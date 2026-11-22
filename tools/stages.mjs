// Follow-up stages after level-wise growth, using the team's Method A protocol
// (main: method_a_local_optimality_report.md) on the U/W families. Each command handles every family given.
//   node tools/stages.mjs <stage> <search-U.json> <search-W.json>
//   A   pair expansion: nodes leaf-only growth could never make internal (de(min), urgent qo / w(max)),
//       added with one child; w grid includes 0.75, 2.45 and (urgent p95 target - empty travel time)
//   A2  confirm on train seeds 1065-1264: growth + A candidates with d < +2.5 or LCB95 < 0 vs family best
//   B   Method A neighborhood of the top 3 confirmed trees: N1 one node, N2 two nodes, STRUCT, W_PAIR
//   B2  confirm B on 1065-1264: screen survivors (d < +2.5 or LCB95 < 0 vs their start tree)
//   C   held-out seeds 2001-2200: B2 survivors (d < +0.5 or LCB95 < 0), starts, 1,185 tree, D0; fleets 15-25 for the best
//   D   fresh seeds 3001-4000: each family's final and the 1,185 tree at 18 and 19 vehicles
// d = candidate - reference cost on the same seeds (negative is better). LCB95 = d - 1.96 SE.
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { gunzipSync } from "node:zlib";
import { GRID, parseTree, treeText, notation, emptySlots, expansionsAt, pathTo, MAX_NODES, MAX_DEPTH } from "./tree-lib.mjs";

const [stage, ...statePaths] = process.argv.slice(2);
const home = dirname(statePaths[0]);
const root = join(home, "..", "..");
const out = p => join(process.env.STAGES_OUT ?? home, p);
const dry = !!process.env.STAGES_OUT;
// Round 2 (de/dl/qo knowledge, U/W/dl/qo GP) uses its own held-out and fresh seeds and output names.
const XC = process.env.STAGE_XC ?? "X01-fresh", XD = process.env.STAGE_XD ?? "X02-final";
const SEEDS_C = process.env.SEEDS_C ?? "2001-2200", SEEDS_D = process.env.SEEDS_D ?? "3001-4000";
const mean = xs => xs.reduce((a, b) => a + b, 0) / xs.length;
const seStat = xs => Math.sqrt(xs.reduce((a, x) => a + (x - mean(xs)) ** 2, 0) / (xs.length - 1) / xs.length);
const paired = (a, b) => { const d = a.map((x, i) => x - b[i]); const m = mean(d), s = seStat(d); return { d: m, se: s, t: m / (s || 1e-9), lcb: m - 1.96 * s }; };
const skeleton = n => n.replace(/[\d.]+\)/g, ")");
const around = f => [f - 1, f, f + 1].filter(x => x >= 1 && x <= 30);
const span = (a, b) => Array.from({ length: b - a + 1 }, (_, i) => a + i);
const clone = nodes => new Map([...nodes].map(([id, n]) => [id, { ...n }]));
const loadState = p => JSON.parse(readFileSync(p, "utf8"));
const saveState = (p, s) => { if (!dry) writeFileSync(p, JSON.stringify(s, null, 2) + "\n"); };
const B1185 = () => parseTree(readFileSync(join(root, "experiments", "common", "F01-fleet-survey", "trees", "B1185.csv"), "utf8"));
const D0 = () => parseTree("key,1,de,min,205,0,0\n");

// Team knowledge (Method A report): urgent p95 target minus empty travel time gives a w boundary.
const { ve } = JSON.parse(readFileSync(join(root, "docs", "case.json"), "utf8"));
const P95_TARGET = 5.2;
const W_KNOW = [0.75, 2.45, ...[25, 50, 65, 90, 115].map(de => Math.round((P95_TARGET - de / ve) * 20) / 20)];
const W_GRID = [...new Set([...GRID.w, ...W_KNOW])].sort((a, b) => a - b);
const GRID_A = { ...GRID, w: W_GRID };
// Discrete rule set for N2 (22 rules, like Method A's S22).
const S22 = [["de", "min", 25], ["de", "min", 50], ["de", "min", 65], ["de", "min", 90], ["de", "max", 65], ["de", "max", 115],
  ["dl", "min", 25], ["dl", "min", 50], ["dl", "min", 65], ["dl", "max", 25], ["dl", "max", 65],
  ["qo", "max", 1], ["qo", "max", 2], ["qo", "max", 4], ["qo", "min", 1],
  ["w", "min", 0.75], ["w", "min", 2.45], ["w", "max", 4], ["w", "max", 8], ["w", "min", 5],
  ["urg", "max", 0.5], ["urg", "min", 0.5]];
const N1_RULES = [...Object.entries(GRID_A).flatMap(([f, ths]) => ths.flatMap(th => [[f, "min", th], [f, "max", th]]))];

function rows(dir) {
  const raw = join(dir, "results", "raw.jsonl.gz");
  if (!existsSync(raw)) throw new Error("No results in " + dir);
  return gunzipSync(readFileSync(raw)).toString().split("\n").filter(Boolean).map(l => JSON.parse(l))
    .map(r => ({ ...r, dir, ok: r.cost.every(c => c !== null) }));
}
const treeOf = r => parseTree(readFileSync(join(r.dir, "trees", r.tree), "utf8"));
// Best non-rejected fleet per tree in a set of rows.
function bestRows(list, rootFeature) {
  const m = new Map();
  for (const r of list) {
    if (!r.ok) continue;
    r.avg ??= mean(r.cost);
    if (!m.has(r.notation) || r.avg < m.get(r.notation).avg) m.set(r.notation, r);
  }
  return [...m.values()].map(r => ({ ...r, nodes: treeOf(r) }))
    .filter(r => !rootFeature || r.nodes.get(1).feature === rootFeature).sort((a, b) => a.avg - b.avg);
}
const validTree = nodes => [...nodes.values()].every(x => {
  const p = pathTo(nodes, x.id, "lo").map(q => q.feature);
  return new Set(p).size === p.length && p.length <= MAX_DEPTH;
}) && nodes.size <= MAX_NODES;

function write(dir, trees, seeds, title, lines, baseline) {
  mkdirSync(join(dir, "trees"), { recursive: true });
  const perTree = {}, names = {};
  let i = 0;
  for (const [key, { nodes, fleets, tag }] of trees) {
    const file = tag ? `${tag}.csv` : `t${String(++i).padStart(5, "0")}.csv`;
    writeFileSync(join(dir, "trees", file), treeText(nodes));
    perTree[file] = [...fleets].sort((a, b) => a - b);
    names[file] = key;
  }
  writeFileSync(join(dir, "trees.json"), JSON.stringify(perTree) + "\n");
  writeFileSync(join(dir, "run.json"), JSON.stringify({ status: "ready", fleets: [], seeds, ...(baseline ? { baseline } : {}) }, null, 2) + "\n");
  const evals = Object.values(perTree).reduce((a, f) => a + f.length, 0);
  writeFileSync(join(dir, "README.md"), `# ${title}\n\n${lines.map(l => "- " + l).join("\n")}\n- 트리 ${trees.size}개 · 평가 ${evals}회 · 시드 ${seeds}\n`);
  console.log(`${dir.split(/[\\/]/).pop()}: ${trees.size} trees, ${evals} evaluations`);
  return evals;
}
function add(map, nodes, fleets, tag) {
  if (!validTree(nodes)) return;
  const key = notation(nodes);
  const e = map.get(key) ?? { nodes, fleets: new Set(), tag };
  fleets.forEach(f => e.fleets.add(f));
  if (tag) e.tag = tag;
  map.set(key, e);
}
const refs = map => { add(map, B1185(), [18, 19], "ref_B1185"); };

// ---------- A: pair expansion ----------
function stageA(sp) {
  const st = loadState(sp);
  // Experiment-method families (GP) skip the knowledge-driven pair expansion.
  if (st.method === "experiment") { console.log(`${st.family}: experiment method, no stage A`); return; }
  const all = bestRows(st.layers.filter(l => existsSync(join(home, l, "results", "raw.jsonl.gz"))).flatMap(l => rows(join(home, l))), st.root);
  const seen = new Set(all.map(r => r.notation));
  const count = new Map(), starts = [];
  for (const t of all) {
    if (t.nodes.size > MAX_NODES - 2) continue;
    const s = skeleton(t.notation);
    if ((count.get(s) ?? 0) >= 3) continue;
    count.set(s, (count.get(s) ?? 0) + 1);
    starts.push(t);
    if (starts.length >= 25) break;
  }
  const map = new Map();
  for (const t of starts)
    for (const [pid, side] of emptySlots(t.nodes)) {
      const path = pathTo(t.nodes, pid, side);
      if (path.length + 2 > MAX_DEPTH) continue;
      const used = new Set(path.map(p => p.feature));
      const urgent = path.some(p => p.feature === "urg" && p.side === "hi");
      const blocked = [];
      if (!used.has("de")) GRID.de.forEach(th => blocked.push(["de", "min", th]));
      if (urgent && !used.has("qo")) for (const m of ["min", "max"]) GRID.qo.forEach(th => blocked.push(["qo", m, th]));
      if (urgent && !used.has("w")) W_GRID.forEach(th => blocked.push(["w", "max", th]));
      for (const [feature, mode, threshold] of blocked) {
        const base = clone(t.nodes);
        const id = base.size + 1;
        base.get(pid)[side === "lo" ? "low" : "high"] = id;
        base.set(id, { id, feature, mode, threshold, low: 0, high: 0 });
        for (const cs of ["lo", "hi"])
          for (const { nodes } of expansionsAt(base, id, cs, Object.keys(GRID_A), GRID_A).out)
            if (!seen.has(notation(nodes))) add(map, nodes, around(t.fleet));
      }
    }
  const id = `${st.family}A1`;
  write(out(id), map, st.seeds, `${id} ${st.family} 계열 짝 확장`, [
    "한 개씩 붙이는 확장으로는 내부 노드가 될 수 없던 규칙을 자식 규칙과 함께 추가: de(min), 긴급 가지의 qo·w(max)",
    `w 격자: 기본 7개 + 팀 진단값 0.75·2.45분 + (긴급 95분위 목표 ${P95_TARGET}분 − 빈 차 이동시간)`,
    `시작 트리: 성장 단계 상위 ${starts.length}개(같은 골격 최대 3개, 노드 7개 이하) · 대수는 시작 트리 최적 ±1`]);
  st.stages = { ...(st.stages ?? {}), A: id };
  saveState(sp, st);
}

// ---------- confirmations on train seeds 1065-1264 ----------
function confirm(sp, fromDirs, refRow, tag, rule, label) {
  const st = loadState(sp);
  const cand = bestRows(fromDirs.flatMap(d => rows(d)), st.root);
  const keep = cand.filter(c => {
    const { d, lcb } = paired(c.cost, refRow.cost);
    return rule === "screen" ? (d < 2.5 || lcb < 0) : (d < 0.5 || lcb < 0);
  }).slice(0, 3000);
  const map = new Map();
  add(map, treeOf(refRow), [refRow.fleet], "ref_family");
  for (const c of keep) add(map, c.nodes, around(c.fleet));
  refs(map);
  const id = `${st.family}${tag}`;
  write(out(id), map, "1065-1264", `${id} ${st.family} 계열 ${label} 확인(학습 시드 200개)`, [
    `대상: ${fromDirs.map(d => d.split(/[\\/]/).pop()).join(", ")}의 후보 중 기준 대비 d < +2.5 또는 LCB95 < 0 (반려 없음) ${keep.length}개${keep.length === 3000 ? " (상한 3,000)" : ""}`,
    `기준(ref_family): ${refRow.notation} · ${refRow.fleet}대. 1,185안(ref_B1185) 18·19대 포함`]);
  st.stages = { ...(st.stages ?? {}), [tag]: id };
  saveState(sp, st);
}
function stageA2(sp) {
  const st = loadState(sp);
  const growth = st.layers.map(l => join(home, l)).filter(d => existsSync(join(d, "results", "raw.jsonl.gz")));
  const ref = bestRows(growth.flatMap(d => rows(d)), st.root)[0];
  confirm(sp, [...growth.slice(-3), ...(st.stages?.A ? [join(home, st.stages.A)] : [])], ref, "A2", "screen", "성장·짝 확장");
}

// Rank inside one confirm experiment, paired against its ref_family rows.
function confirmed(dir, rootFeature, rule) {
  const all = rows(dir);
  const ref = all.filter(r => r.tree === "ref_family.csv" && r.ok).sort((a, b) => mean(a.cost) - mean(b.cost))[0];
  return { ref, list: bestRows(all.filter(r => !/^ref_(B1185|D0)/.test(r.tree)), rootFeature)
    .map(r => ({ ...r, ...paired(r.cost, ref.cost) }))
    .filter(r => rule === "screen" ? (r.d < 2.5 || r.lcb < 0) : (r.d < 0.5 || r.lcb < 0)) };
}

// ---------- B: Method A neighborhood ----------
function neighborhood(t) {
  const map = new Map(), fl = [t.fleet];
  const ids = [...t.nodes.keys()];
  const set = (nodes, id, [feature, mode, threshold]) => { Object.assign(nodes.get(id), { feature, mode, threshold }); return nodes; };
  // N1: every rule at every node (root keeps its family feature), fine w around current values, leaf removal.
  for (const id of ids) {
    const n = t.nodes.get(id);
    const rules = [...N1_RULES];
    if (n.feature === "w") for (let k = -8; k <= 8; k++) rules.push(["w", n.mode, Math.round((n.threshold + k * 0.05) * 100) / 100]);
    for (const r of rules) {
      if (id === 1 && r[0] !== n.feature) continue;
      if (r[2] <= 0) continue;
      add(map, set(clone(t.nodes), id, r), fl);
    }
    if (!n.low && !n.high && id !== 1) add(map, removeLeaf(t.nodes, id), fl);
  }
  // N2: two nodes changed at once over the 22-rule set.
  for (let i = 0; i < ids.length; i++) for (let j = i + 1; j < ids.length; j++)
    for (const a of S22) for (const b of S22) {
      if ((ids[i] === 1 && a[0] !== t.nodes.get(1).feature)) continue;
      add(map, set(set(clone(t.nodes), ids[i], a), ids[j], b), fl);
    }
  // STRUCT: swap a node's lo/hi subtrees; move a leaf to another empty slot.
  for (const id of ids) {
    const v = clone(t.nodes), n = v.get(id);
    if (n.low || n.high) { [n.low, n.high] = [n.high, n.low]; add(map, v, fl); }
    if (!n.low && !n.high && id !== 1) {
      const base = removeLeaf(t.nodes, id);
      for (const [pid, side] of emptySlots(base)) {
        const w = clone(base), nid = w.size + 1;
        w.get(pid)[side === "lo" ? "low" : "high"] = nid;
        w.set(nid, { ...t.nodes.get(id), id: nid, low: 0, high: 0 });
        add(map, w, fl);
      }
    }
  }
  // W_PAIR: two w nodes on a joint 0.05-minute grid (±0.25).
  const wIds = ids.filter(id => t.nodes.get(id).feature === "w");
  for (let i = 0; i < wIds.length; i++) for (let j = i + 1; j < wIds.length; j++)
    for (let a = -5; a <= 5; a++) for (let b = -5; b <= 5; b++) {
      const v = clone(t.nodes);
      v.get(wIds[i]).threshold = Math.round((t.nodes.get(wIds[i]).threshold + a * 0.05) * 100) / 100;
      v.get(wIds[j]).threshold = Math.round((t.nodes.get(wIds[j]).threshold + b * 0.05) * 100) / 100;
      if (v.get(wIds[i]).threshold > 0 && v.get(wIds[j]).threshold > 0) add(map, v, fl);
    }
  map.delete(notation(t.nodes));
  return map;
}
function removeLeaf(nodes, id) {
  const v = clone(nodes);
  for (const x of v.values()) { if (x.low === id) x.low = 0; if (x.high === id) x.high = 0; }
  v.delete(id);
  const m = new Map([...v.keys()].sort((a, b) => a - b).map((old, i) => [old, i + 1]));
  return new Map([...v.values()].map(x => [m.get(x.id), { ...x, id: m.get(x.id), low: m.get(x.low) ?? 0, high: m.get(x.high) ?? 0 }]));
}
function stageB(sp) {
  const st = loadState(sp);
  const { list } = confirmed(join(home, st.stages.A2), st.root, "screen");
  const starts = [], seenSk = new Set();
  for (const t of list) { if (seenSk.has(skeleton(t.notation))) continue; seenSk.add(skeleton(t.notation)); starts.push(t); if (starts.length >= 3) break; }
  const map = new Map();
  starts.forEach((t, k) => {
    add(map, t.nodes, [t.fleet], `start${k + 1}`);
    for (const [key, e] of neighborhood(t)) if (!map.has(key)) map.set(key, e);
  });
  const id = `${st.family}B1`;
  write(out(id), map, st.seeds, `${id} ${st.family} 계열 방법 A 이웃`, [
    `시작: A2 확인 상위 ${starts.length}개(서로 다른 골격) — ${starts.map(s => s.notation + " · " + s.fleet + "대").join(" / ")}`,
    "N1 노드 1개를 모든 규칙으로(w는 현재값 ±0.40분을 0.05분 간격 추가), 말단 제거",
    "N2 노드 2개를 22개 규칙 집합으로 동시 변경 · STRUCT 좌우 가지 교환, 말단 이동 · W_PAIR w 노드 두 개 ±0.25분 0.05분 격자",
    "대수는 시작 트리 최적 대수 고정(대수 재확인은 C 단계)"]);
  st.stages = { ...(st.stages ?? {}), B: id, Bstarts: starts.map(s => ({ notation: s.notation, fleet: s.fleet })) };
  saveState(sp, st);
}
function stageB2(sp) {
  const st = loadState(sp);
  const bRows = rows(join(home, st.stages.B));
  const startRows = bRows.filter(r => r.tree.startsWith("start") && r.ok);
  // Screen each neighbor against its own start (the nearest start by notation prefix is not known; use the best start).
  const ref = startRows.sort((a, b) => mean(a.cost) - mean(b.cost))[0];
  confirm(sp, [join(home, st.stages.B)], ref, "B2", "screen", "방법 A 이웃");
}

// ---------- C: held-out seeds 2001-2200 ----------
function stageC() {
  const map = new Map(), lines = [];
  for (const sp of statePaths) {
    const st = loadState(sp);
    const { ref, list } = confirmed(join(home, st.stages.B2), st.root, "final");
    const a2 = confirmed(join(home, st.stages.A2), st.root, "final").list.slice(0, 5);
    const picks = [...list.slice(0, 25), ...a2];
    add(map, treeOf(ref), span(15, 25), `${st.family}_ref`);
    picks.forEach((t, i) => add(map, t.nodes, i < 3 ? span(15, 25) : around(t.fleet)));
    lines.push(`${st.family}: B2에서 d < +0.5 또는 LCB95 < 0인 후보 상위 ${Math.min(25, list.length)}개 + A2 상위 5개 + 계열 기준(${st.family}_ref). 상위 3개와 기준은 15~25대`);
  }
  add(map, B1185(), span(15, 25), "ref_B1185");
  add(map, D0(), span(15, 25), "ref_D0");
  lines.push("1,185안과 D0를 15~25대 전부", "판정: 계열 기준 대비 t < −2이고 반려 없음일 때만 개선으로 인정, 1,185안 대비 차이와 단측 95% 하한 보고");
  write(out(XC), map, SEEDS_C, `${XC} 보류 시드 확인과 대수 재확인`, lines);
}

// ---------- D: fresh seeds 3001-4000 ----------
function stageD() {
  const map = new Map(), lines = [];
  const x = rows(join(home, XC));
  const best = bestRows(x.filter(r => !r.tree.startsWith("ref_")));
  for (const sp of statePaths) {
    const st = loadState(sp);
    const fam = best.filter(r => r.nodes.get(1).feature === st.root)[0];
    if (fam) { add(map, fam.nodes, [fam.fleet], `${st.family}_final`); lines.push(`${st.family} 최종: ${fam.notation} · ${fam.fleet}대 (X01 평균 ${fam.avg.toFixed(2)})`); }
  }
  add(map, B1185(), [18, 19], "ref_B1185");
  lines.push("1,185안 18·19대: 제출 대수 재확인 겸 기준", "강건성: 반려율, 평균, 표준편차, 나쁜 쪽 5% 비용");
  write(out(XD), map, SEEDS_D, `${XD} 새 시드 최종 확인`, lines);
}

const per = { A: stageA, A2: stageA2, B: stageB, B2: stageB2 };
if (per[stage]) statePaths.forEach(sp => per[stage](sp));
else if (stage === "C") stageC();
else if (stage === "D") stageD();
else { console.error("Usage: node tools/stages.mjs A|A2|B|B2|C|D <search-U.json> <search-W.json>"); process.exit(1); }
