// Selective level-wise growth for one root family.
//   node tools/grow.mjs experiments/JSM/search-U.json
// Reads the newest layer's results, keeps every tree that is not significantly worse than the
// family's best so far, and writes the next layer (all empty slots of each survivor, one node each).
import { existsSync, mkdirSync, readdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { gunzipSync } from "node:zlib";
import { parseTree, readTree, treeText, notation, emptySlots, expansionsAt } from "./tree-lib.mjs";

const statePath = process.argv[2];
const state = JSON.parse(readFileSync(statePath, "utf8"));
const home = dirname(statePath);
const save = () => writeFileSync(statePath, JSON.stringify(state, null, 2) + "\n");
if (state.done) { console.log(`${state.family}: done`); process.exit(0); }

if (state.method === "experiment") {
  // GP families: one evaluation layer built from experiments/JSM/GP/<root>-<fleet>/gp_top*.csv, then done.
  const gpHome = join(home, "GP");
  if (!state.layers.length) {
    const runs = existsSync(gpHome) ? readdirSync(gpHome).filter(d => d.startsWith(state.root + "-")) : [];
    if (runs.length < state.gpRuns) { console.log(`${state.family}: waiting for GP (${runs.length}/${state.gpRuns})`); process.exit(0); }
    const id = `${state.family}01-gp`, dir = join(home, id);
    mkdirSync(join(dir, "trees"), { recursive: true });
    const trees = new Map();
    for (const r of runs) for (const f of readdirSync(join(gpHome, r)).filter(f => /^gp_top\d+\.csv$/.test(f))) {
      const nodes = parseTree(readFileSync(join(gpHome, r, f), "utf8"));
      if (nodes.get(1).feature !== state.root) continue;
      trees.set(notation(nodes), nodes);
    }
    const perTree = {};
    let i = 0;
    for (const nodes of trees.values()) {
      const file = `t${String(++i).padStart(5, "0")}.csv`;
      writeFileSync(join(dir, "trees", file), treeText(nodes));
      perTree[file] = state.fleets;
    }
    writeFileSync(join(dir, "trees.json"), JSON.stringify(perTree) + "\n");
    writeFileSync(join(dir, "run.json"), JSON.stringify({ status: "ready", fleets: [], seeds: state.seeds }, null, 2) + "\n");
    writeFileSync(join(dir, "README.md"), `# ${id} ${state.family} 계열 GP 결과 평가

- 루트 ${state.root} 고정, 백지 시작 GP(main agv_opt.cpp run_reconstruct) ${runs.join(", ")}의 상위 트리 ${trees.size}개
- 대수 ${state.fleets[0]}~${state.fleets.at(-1)} · 시드 ${state.seeds}
`);
    state.layers.push(id);
    save();
    console.log(`${state.family}: GP layer ${id} with ${trees.size} trees`);
    process.exit(0);
  }
  const l = state.layers.at(-1);
  if (!existsSync(join(home, l, "results", "summary.csv"))) { console.log(`${state.family}: waiting for ${l}`); process.exit(0); }
  const best = gunzipSync(readFileSync(join(home, l, "results", "raw.jsonl.gz"))).toString().split("\n").filter(Boolean)
    .map(x => JSON.parse(x)).filter(r => r.cost.every(c => c !== null))
    .map(r => ({ ...r, avg: r.cost.reduce((a, b) => a + b, 0) / r.cost.length })).sort((a, b) => a.avg - b.avg)[0];
  if (best) state.ref = { layer: l, tree: best.tree, notation: best.notation, fleet: best.fleet, avg: best.avg, cost: best.cost };
  state.history = [{ layer: l, evaluated: 0, survivors: 0, layerBest: best ? { notation: best.notation, fleet: best.fleet, avg: +best.avg.toFixed(2) } : null, nextTrees: 0 }];
  state.done = true;
  save();
  console.log(`${state.family}: GP layer evaluated, done`);
  process.exit(0);
}

const layer = state.layers.at(-1);
const layerDir = join(home, layer);
if (!existsSync(join(layerDir, "results", "summary.csv"))) { console.log(`${state.family}: waiting for ${layer}`); process.exit(0); }

const mean = xs => xs.reduce((a, b) => a + b, 0) / xs.length;
const se = xs => Math.sqrt(xs.reduce((a, x) => a + (x - mean(xs)) ** 2, 0) / (xs.length - 1) / xs.length);
const rows = gunzipSync(readFileSync(join(layerDir, "results", "raw.jsonl.gz"))).toString()
  .split("\n").filter(Boolean).map(l => JSON.parse(l));

// Best fleet per tree, counting only fleets with no rejected seed; only trees of this family's root.
const best = new Map();
for (const r of rows) {
  if (r.cost.some(c => c === null)) continue;
  const tree = parseTree(readFileSync(join(layerDir, "trees", r.tree), "utf8"));
  if (tree.get(1).feature !== state.root) continue;
  const avg = mean(r.cost);
  if (!best.has(r.tree) || avg < best.get(r.tree).avg) best.set(r.tree, { ...r, avg, nodesMap: tree });
}
const ranked = [...best.values()].sort((a, b) => a.avg - b.avg);
if (ranked.length && (!state.ref || ranked[0].avg < state.ref.avg))
  state.ref = { layer, tree: ranked[0].tree, notation: ranked[0].notation, fleet: ranked[0].fleet, avg: ranked[0].avg, cost: ranked[0].cost };

// A tree whose costs equal an already evaluated tree's at the same fleet made no different decision
// on these seeds (decision impact 0, design §10): keep only the first, simpler one.
const sig = r => `${r.fleet}|${r.cost.join(",")}`;
const known = new Set();
for (const earlier of state.layers.slice(0, -1)) {
  const file = join(home, earlier, "results", "raw.jsonl.gz");
  if (existsSync(file)) gunzipSync(readFileSync(file)).toString().split("\n").filter(Boolean)
    .forEach(l => known.add(sig(JSON.parse(l))));
}
const distinct = [];
let duplicates = 0;
for (const r of ranked) {
  if (known.has(sig(r))) { duplicates++; continue; }
  known.add(sig(r));
  distinct.push(r);
}

// Survive unless significantly worse than the family best (paired over the same seeds).
let survivors = distinct.filter(r => {
  const d = r.cost.map((c, i) => c - state.ref.cost[i]);
  return mean(d) - 2 * se(d) <= 0;
});
const seen = new Set(state.seen ?? []);
for (const r of rows) seen.add(r.notation);

// Next layer: every empty slot of every survivor, deduplicated against all evaluated trees.
function nextLayer(list) {
  const trees = new Map();
  for (const s of list)
    for (const [id, side] of emptySlots(s.nodesMap))
      for (const { nodes } of expansionsAt(s.nodesMap, id, side).out) {
        const key = notation(nodes);
        if (seen.has(key)) continue;
        const fleets = [s.fleet - 1, s.fleet, s.fleet + 1].filter(f => f >= 1 && f <= 30);
        const entry = trees.get(key) ?? { nodes, fleets: new Set() };
        fleets.forEach(f => entry.fleets.add(f));
        trees.set(key, entry);
      }
  return trees;
}
let next = nextLayer(survivors);
const evals = t => [...t.values()].reduce((a, e) => a + e.fleets.size, 0);
let capped = null;
while (state.maxEvaluations && evals(next) > state.maxEvaluations && survivors.length > 1) {
  survivors = survivors.slice(0, Math.max(1, Math.floor(survivors.length * 0.8)));
  next = nextLayer(survivors);
  capped = survivors.length;
}

state.history = state.history ?? [];
state.history.push({ layer, evaluated: rows.length, familyTrees: ranked.length, sameAsEarlier: duplicates,
  layerBest: ranked[0] ? { notation: ranked[0].notation, fleet: ranked[0].fleet, avg: +ranked[0].avg.toFixed(2) } : null,
  familyBest: { notation: state.ref.notation, fleet: state.ref.fleet, avg: +state.ref.avg.toFixed(2) },
  survivors: survivors.length, cappedTo: capped, nextTrees: next.size, nextEvaluations: evals(next) });
state.seen = [...seen];

if (!next.size) {
  state.done = true; save();
  console.log(`${state.family}: no further expansion, done`); process.exit(0);
}
const id = `${state.family}${String(state.layers.length + 1).padStart(2, "0")}`;
const dir = join(home, id);
mkdirSync(join(dir, "trees"), { recursive: true });
const perTree = {};
let i = 0;
for (const [key, { nodes, fleets }] of next) {
  const file = `t${String(++i).padStart(5, "0")}.csv`;
  writeFileSync(join(dir, "trees", file), treeText(nodes));
  perTree[file] = [...fleets].sort((a, b) => a - b);
}
writeFileSync(join(dir, "trees.json"), JSON.stringify(perTree) + "\n");
writeFileSync(join(dir, "run.json"), JSON.stringify({ status: "ready", fleets: [], seeds: state.seeds }, null, 2) + "\n");
writeFileSync(join(dir, "README.md"), `# ${id} ${state.family} 계열 자동 확장

- 생성: tools/grow.mjs · 부모 층: ${layer}
- 부모 생존 트리 ${survivors.length}개${capped ? ` (평가 수 상한으로 ${capped}개까지 축소)` : ""}의 모든 빈 자리에 규칙 1개씩
- 트리 ${next.size}개 · 평가 ${evals(next)}회 · 대수는 부모 최적 대수 ±1 · 시드 ${state.seeds}
- 생존 기준: 반려 없음, 계열 최선과의 시드별 차이가 2 표준오차 안
`);
state.layers.push(id);
save();
console.log(`${state.family}: ${layer} -> ${id}: ${survivors.length} survivors, ${next.size} trees, ${evals(next)} evaluations`);
