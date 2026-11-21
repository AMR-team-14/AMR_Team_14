// Follow-up stages after level-wise growth (docs/notes/experiment-design.md §9, §11).
//   node tools/stages.mjs pair     <search-U.json> [--top 25]   A: add blocked nodes (de(min), urgent qo / w(max)) with a child
//   node tools/stages.mjs refine   <search-U.json> [--top 20]   B: re-tune every node of the best trees
//   node tools/stages.mjs validate <out-dir> <search-*.json...>  C: best trees + 1,185 baseline on fresh seeds 2001-2200
// Each command writes one experiment folder with run.json "ready" and records it in the state's "extra".
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { gunzipSync } from "node:zlib";
import { GRID, parseTree, treeText, notation, emptySlots, expansionsAt, pathTo, MAX_NODES, MAX_DEPTH } from "./tree-lib.mjs";

const [cmd, ...rest] = process.argv.slice(2);
const opt = (name, d) => { const i = rest.indexOf(name); return i < 0 ? d : +rest[i + 1]; };
const mean = xs => xs.reduce((a, b) => a + b, 0) / xs.length;
const skeleton = n => n.replace(/[\d.]+\)/g, ")");
const outBase = home => process.env.STAGES_OUT ?? home;   // dry runs write elsewhere and keep the state
const saveState = (path, state) => { if (!process.env.STAGES_OUT) writeFileSync(path, JSON.stringify(state, null, 2) + "\n"); };
const fleetsAround = f => [f - 1, f, f + 1].filter(x => x >= 1 && x <= 30);

// Every evaluated tree of a family (growth layers + earlier extra stages), best non-rejected fleet each.
function collect(state, home) {
  const best = new Map();
  for (const dir of [...state.layers, ...(state.extra ?? [])]) {
    const raw = join(home, dir, "results", "raw.jsonl.gz");
    if (!existsSync(raw)) continue;
    for (const l of gunzipSync(readFileSync(raw)).toString().split("\n").filter(Boolean)) {
      const r = JSON.parse(l);
      if (r.cost.some(c => c === null)) continue;
      const avg = mean(r.cost);
      if (!best.has(r.notation) || avg < best.get(r.notation).avg)
        best.set(r.notation, { dir, tree: r.tree, notation: r.notation, fleet: r.fleet, avg, cost: r.cost });
    }
  }
  return [...best.values()]
    .map(b => ({ ...b, nodes: parseTree(readFileSync(join(home, b.dir, "trees", b.tree), "utf8")) }))
    .filter(b => b.nodes.get(1).feature === state.root)
    .sort((a, b) => a.avg - b.avg);
}
// Top k with at most `per` trees of the same skeleton (structure without thresholds).
function diverseTop(list, k, per = 3, maxNodes = MAX_NODES) {
  const count = new Map(), out = [];
  for (const t of list) {
    if (t.nodes.size > maxNodes) continue;
    const s = skeleton(t.notation);
    if ((count.get(s) ?? 0) >= per) continue;
    count.set(s, (count.get(s) ?? 0) + 1);
    out.push(t);
    if (out.length >= k) break;
  }
  return out;
}
const clone = nodes => new Map([...nodes].map(([id, n]) => [id, { ...n }]));

function writeExperiment(dir, trees, seeds, title, lines) {
  mkdirSync(join(dir, "trees"), { recursive: true });
  const perTree = {};
  let i = 0;
  for (const { nodes, fleets } of trees.values()) {
    const file = `t${String(++i).padStart(5, "0")}.csv`;
    writeFileSync(join(dir, "trees", file), treeText(nodes));
    perTree[file] = [...fleets].sort((a, b) => a - b);
  }
  writeFileSync(join(dir, "trees.json"), JSON.stringify(perTree) + "\n");
  writeFileSync(join(dir, "run.json"), JSON.stringify({ status: "ready", fleets: [], seeds }, null, 2) + "\n");
  const evals = Object.values(perTree).reduce((a, f) => a + f.length, 0);
  writeFileSync(join(dir, "README.md"), `# ${title}\n\n${lines.map(l => "- " + l).join("\n")}\n- 트리 ${i}개 · 평가 ${evals}회 · 시드 ${seeds}\n`);
  return { trees: i, evals };
}
function add(map, nodes, fleets, seen) {
  const key = notation(nodes);
  if (seen?.has(key)) return;
  const e = map.get(key) ?? { nodes, fleets: new Set() };
  fleets.forEach(f => e.fleets.add(f));
  map.set(key, e);
}

// A: nodes the leaf-only growth could never place as internal nodes, added together with one child.
function pair(statePath) {
  const state = JSON.parse(readFileSync(statePath, "utf8")), home = dirname(statePath);
  const seen = new Set(state.seen ?? []);
  const starts = diverseTop(collect(state, home), opt("--top", 25), 3, MAX_NODES - 2);
  const out = new Map();
  for (const t of starts)
    for (const [pid, side] of emptySlots(t.nodes)) {
      const path = pathTo(t.nodes, pid, side);
      if (path.length + 2 > MAX_DEPTH) continue;
      const used = new Set(path.map(p => p.feature));
      const urgent = path.some(p => p.feature === "urg" && p.side === "hi");
      const blocked = [];
      if (!used.has("de")) GRID.de.forEach(th => blocked.push(["de", "min", th]));
      if (urgent && !used.has("qo")) for (const m of ["min", "max"]) GRID.qo.forEach(th => blocked.push(["qo", m, th]));
      if (urgent && !used.has("w")) GRID.w.forEach(th => blocked.push(["w", "max", th]));
      for (const [feature, mode, threshold] of blocked) {
        const base = clone(t.nodes);
        const id = base.size + 1;
        base.get(pid)[side === "lo" ? "low" : "high"] = id;
        base.set(id, { id, feature, mode, threshold, low: 0, high: 0 });
        for (const childSide of ["lo", "hi"])
          for (const { nodes } of expansionsAt(base, id, childSide).out) add(out, nodes, fleetsAround(t.fleet), seen);
      }
    }
  const id = `${state.family}A1`;
  const res = writeExperiment(join(outBase(home), id), out, state.seeds, `${id} ${state.family} 계열 짝 확장`, [
    "한 개씩 붙이는 확장으로는 내부 노드가 될 수 없던 규칙을 자식 규칙과 함께 추가: de(min), 긴급 가지의 qo·w(max)",
    `시작 트리: 계열 상위 ${starts.length}개(같은 골격 최대 3개, 노드 7개 이하) · 대수는 시작 트리 최적 ±1`]);
  state.extra = [...(state.extra ?? []), id];
  saveState(statePath, state);
  console.log(`${id}: ${starts.length} starts, ${res.trees} trees, ${res.evals} evaluations`);
}

// B: one-change neighbors of the best trees: threshold on a fine grid, direction, variable, leaf removal.
const FINE = {
  de: GRID.de, dl: GRID.dl, urg: [0.5],
  w: [0.25, 0.5, 0.75, 1, 1.25, 1.5, 2, 2.5, 3, 3.5, 4, 5, 6, 7, 8, 10, 12, 16, 20, 24, 32],
  qo: [1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 16]
};
function refine(statePath) {
  const state = JSON.parse(readFileSync(statePath, "utf8")), home = dirname(statePath);
  const seen = new Set(collect(state, home).map(t => t.notation));
  const starts = diverseTop(collect(state, home), opt("--top", 20), 4);
  const out = new Map();
  for (const t of starts) {
    for (const n of t.nodes.values()) {
      for (const threshold of FINE[n.feature]) for (const mode of ["min", "max"]) {
        if (threshold === n.threshold && mode === n.mode) continue;
        const v = clone(t.nodes); Object.assign(v.get(n.id), { threshold, mode });
        add(out, v, fleetsAround(t.fleet), seen);
      }
      // Swap the variable when no ancestor or descendant on any path through this node uses it.
      for (const feature of Object.keys(FINE)) {
        if (feature === n.feature || n.id === 1) continue;
        const v = clone(t.nodes); v.get(n.id).feature = feature;
        const ok = [...v.values()].every(x => {
          const p = pathTo(v, x.id, "lo").map(q => q.feature);
          return new Set(p).size === p.length;
        });
        if (!ok) continue;
        for (const threshold of FINE[feature]) for (const mode of ["min", "max"]) {
          const w = clone(v); Object.assign(w.get(n.id), { threshold, mode });
          add(out, w, fleetsAround(t.fleet), seen);
        }
      }
      // Remove a leaf (renumber so ids stay 1..n).
      if (!n.low && !n.high && n.id !== 1) {
        const v = clone(t.nodes);
        for (const x of v.values()) { if (x.low === n.id) x.low = 0; if (x.high === n.id) x.high = 0; }
        v.delete(n.id);
        const map = new Map([...v.keys()].sort((a, b) => a - b).map((old, i) => [old, i + 1]));
        const r = new Map([...v.values()].map(x => [map.get(x.id), { ...x, id: map.get(x.id), low: map.get(x.low) ?? 0, high: map.get(x.high) ?? 0 }]));
        add(out, r, fleetsAround(t.fleet), seen);
      }
    }
  }
  const id = `${state.family}B1`;
  const res = writeExperiment(join(outBase(home), id), out, state.seeds, `${id} ${state.family} 계열 다듬기`, [
    `계열 상위 ${starts.length}개(같은 골격 최대 4개)의 노드 하나씩: 경계를 촘촘한 격자로 변경, 방향 변경, 변수 교체, 말단 제거`,
    "w 격자 0.25~32분 21단계, qo 1~16건 11단계"]);
  state.extra = [...(state.extra ?? []), id];
  saveState(statePath, state);
  console.log(`${id}: ${starts.length} starts, ${res.trees} trees, ${res.evals} evaluations`);
}

// C: the best trees of each family on seeds never used in the search, with the 1,185 baseline and D0.
function validate(outDir, statePaths) {
  const out = new Map();
  const lines = [];
  for (const sp of statePaths) {
    const state = JSON.parse(readFileSync(sp, "utf8"));
    const top = diverseTop(collect(state, dirname(sp)), 10, 3);
    top.forEach((t, i) => add(out, t.nodes, i < 3 ? Array.from({ length: 11 }, (_, k) => 15 + k) : fleetsAround(t.fleet)));
    lines.push(`${state.family} 계열 상위 ${top.length}개(같은 골격 최대 3개). 상위 3개는 15~25대 전부, 나머지는 탐색 최적 ±1`);
  }
  const root = join(dirname(statePaths[0]), "..", "..");
  const base = parseTree(readFileSync(join(root, "experiments", "common", "F01-fleet-survey", "trees", "B1185.csv"), "utf8"));
  add(out, base, Array.from({ length: 11 }, (_, k) => 15 + k));
  add(out, parseTree("key,1,de,min,205,0,0\n"), Array.from({ length: 11 }, (_, k) => 15 + k));
  lines.push("기준: 1,185안과 D0를 15~25대 전부", "탐색에 쓰지 않은 시드라 승자의 저주(같은 시드 재사용으로 최선값이 낙관적으로 나오는 문제)를 뺀 비교");
  const res = writeExperiment(outDir, out, "2001-2200", `${outDir.split(/[\\/]/).pop()} 새 시드 확인과 대수 재확인`, lines);
  console.log(`${outDir}: ${res.trees} trees, ${res.evals} evaluations`);
}

if (cmd === "pair") pair(rest[0]);
else if (cmd === "refine") refine(rest[0]);
else if (cmd === "validate") validate(rest[0], rest.slice(1).filter(a => !a.startsWith("--")));
else { console.error("Usage: node tools/stages.mjs pair|refine <search.json> | validate <out-dir> <search.json...>"); process.exit(1); }
