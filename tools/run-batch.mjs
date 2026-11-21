// Run one experiment folder: every tree in trees/ x its fleets, on the same seeds.
//   node tools/run-batch.mjs experiments/<owner>/<id> [--force]     run and summarize
//   node tools/run-batch.mjs --pending [--shard k/n]                 every ready run.json without results
//   node tools/run-batch.mjs --merge-pending                         summarize shard parts
// Fleets come from run.json "fleets", or per tree from trees.json {"file.csv": [18, 19]}.
import { execFile, execFileSync } from "node:child_process";
import { existsSync, mkdirSync, readdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { cpus } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { promisify } from "node:util";
import { gzipSync } from "node:zlib";
import { readTree, treeText, notation, seedList } from "./tree-lib.mjs";

const run = promisify(execFile);
const root = fileURLToPath(new URL("..", import.meta.url));
const binary = join(root, "build", process.platform === "win32" ? "agv_opt.exe" : "agv_opt");
const fixedDir = JSON.parse(readFileSync(join(root, "docs/case.json"), "utf8")).fixedDir;
const args = process.argv.slice(2);
const opt = name => { const i = args.indexOf(name); return i < 0 ? null : args[i + 1]; };

function readyExperiments() {
  const found = [];
  (function walk(dir) {
    for (const entry of readdirSync(dir, { withFileTypes: true })) {
      const path = join(dir, entry.name);
      if (entry.isDirectory() && !entry.name.startsWith("_")) walk(path);
      else if (entry.name === "run.json") {
        const spec = JSON.parse(readFileSync(path, "utf8"));
        if (spec.status === "ready" && !existsSync(join(dir, "results", "summary.csv"))) found.push(dir);
      }
    }
  })(join(root, "experiments"));
  return found.sort();
}

function jobsOf(dir) {
  const spec = JSON.parse(readFileSync(join(dir, "run.json"), "utf8"));
  const treeDir = join(dir, spec.trees ?? "trees");
  const perTree = existsSync(join(dir, "trees.json")) ? JSON.parse(readFileSync(join(dir, "trees.json"), "utf8")) : {};
  const trees = readdirSync(treeDir).filter(f => f.endsWith(".csv") && !f.startsWith("index_")).sort();
  return { spec, treeDir, trees, jobs: trees.flatMap(tree => (perTree[tree] ?? spec.fleets).map(fleet => ({ tree, fleet }))) };
}

async function pool(items, limit, task) {
  const out = new Array(items.length);
  let next = 0;
  await Promise.all(Array.from({ length: limit }, async () => {
    while (next < items.length) { const i = next++; out[i] = await task(items[i]); }
  }));
  return out;
}

const r3 = x => x === null ? null : Math.round(x * 1000) / 1000;
async function runShard(dir, k = 0, n = 1) {
  const { spec, treeDir, jobs } = jobsOf(dir);
  const seeds = seedList(spec.seeds).join(",");
  const mine = jobs.filter((_, i) => i % n === k);
  const scratch = join(dir, "results", ".csv");
  mkdirSync(join(dir, "results", "parts"), { recursive: true });
  mkdirSync(scratch, { recursive: true });
  const started = Date.now();
  const rows = await pool(mine, cpus().length, async ({ tree, fleet }) => {
    const nodes = readTree(join(treeDir, tree));
    const csv = join(scratch, `${tree.replace(/\.csv$/, "")}-n${fleet}.csv`);
    writeFileSync(csv, `dir,${fixedDir}\nfleet,${fleet}\n${treeText(nodes)}`);
    const { stdout } = await run(binary, ["--eval-json", csv, "--seeds", seeds], { maxBuffer: 1 << 26 });
    const r = JSON.parse(stdout);
    return { tree, notation: notation(nodes), fleet, nodes: nodes.size,
      cost: r.scores.map(s => r3(s.cost)), wait: r.scores.map(s => r3(s.averageWait)),
      u95: r.scores.map(s => r3(s.urgent95)), maxQ: Math.max(...r.scores.map(s => s.maxQueue)),
      endQ: Math.max(...r.scores.map(s => s.endQueue)) };
  });
  writeFileSync(join(dir, "results", "parts", `part-${k}.jsonl`), rows.map(r => JSON.stringify(r)).join("\n") + (rows.length ? "\n" : ""));
  console.log(`${dir} shard ${k}/${n}: ${mine.length} evaluations in ${Math.round((Date.now() - started) / 1000)} s`);
  return (Date.now() - started) / 1000;
}

const mean = xs => xs.reduce((a, b) => a + b, 0) / xs.length;
const sd = xs => xs.length < 2 ? 0 : Math.sqrt(xs.reduce((a, x) => a + (x - mean(xs)) ** 2, 0) / (xs.length - 1));
const q = (xs, p) => { const s = [...xs].sort((a, b) => a - b); return s[Math.min(s.length - 1, Math.ceil(p * s.length) - 1)]; };
const f4 = x => x === null || x === undefined || Number.isNaN(x) ? "" : Number(x.toFixed(4));

function loadRows(dir) {
  const parts = join(dir, "results", "parts");
  return readdirSync(parts).filter(f => f.endsWith(".jsonl")).flatMap(f =>
    readFileSync(join(parts, f), "utf8").split("\n").filter(Boolean).map(l => JSON.parse(l)));
}

function merge(dir, seconds = null) {
  const { spec, trees, jobs } = jobsOf(dir);
  const rows = loadRows(dir);
  if (rows.length !== jobs.length) throw new Error(`${dir}: ${rows.length}/${jobs.length} evaluations present`);
  rows.sort((a, b) => a.tree.localeCompare(b.tree) || a.fleet - b.fleet);
  const base = spec.baseline && new Map(rows.filter(r => r.tree === spec.baseline).map(r => [r.fleet, r]));
  const header = "tree,notation,nodes,fleet,rejected,average,costSD,costP95,avgWait,urgent95,maxQueue,endQueueMax,deltaVsBaseline,deltaSE";
  const lines = rows.map(r => {
    const ok = r.cost.map((c, i) => i).filter(i => r.cost[i] !== null);
    const rejected = r.cost.length - ok.length;
    const costs = ok.map(i => r.cost[i]);
    let delta = null, deltaSE = null;
    const b = base?.get(r.fleet);
    if (b && !rejected && b.cost.every(c => c !== null)) {
      const d = r.cost.map((c, i) => b.cost[i] - c);   // + means cheaper than baseline
      delta = mean(d); deltaSE = sd(d) / Math.sqrt(d.length);
    }
    return [r.tree, `"${r.notation}"`, r.nodes, r.fleet, rejected, rejected ? "" : f4(mean(costs)),
      rejected ? "" : f4(sd(costs)), rejected ? "" : f4(q(costs, 0.95)),
      ok.length ? f4(mean(ok.map(i => r.wait[i]))) : "", ok.length ? f4(mean(ok.map(i => r.u95[i]))) : "",
      r.maxQ, r.endQ, f4(delta), f4(deltaSE)].join(",");
  });
  const results = join(dir, "results");
  writeFileSync(join(results, "raw.jsonl.gz"), gzipSync(rows.map(r => JSON.stringify(r)).join("\n") + "\n"));
  writeFileSync(join(results, "summary.csv"), [header, ...lines].join("\n") + "\n");
  const commit = execFileSync("git", ["rev-parse", "--short", "HEAD"], { cwd: root }).toString().trim();
  writeFileSync(join(results, "meta.json"), JSON.stringify({
    engineCommit: commit, seeds: spec.seeds, trees: trees.length, evaluations: jobs.length,
    runner: process.env.GITHUB_ACTIONS ? "github-actions" : "local", seconds, finishedAt: new Date().toISOString()
  }, null, 2) + "\n");
  rmSync(join(results, "parts"), { recursive: true, force: true });
  console.log(`${dir}: summarized ${rows.length} evaluations`);
}

if (args.includes("--merge-pending")) {
  for (const dir of readyExperiments()) if (existsSync(join(dir, "results", "parts"))) merge(dir);
} else if (args.includes("--pending")) {
  const [k, n] = (opt("--shard") ?? "0/1").split("/").map(Number);
  const dirs = readyExperiments();
  if (!dirs.length) console.log("No experiment to run.");
  for (const dir of dirs) await runShard(dir, k, n);
  if (n === 1) for (const dir of dirs) merge(dir);
} else {
  const dir = args.find(a => !a.startsWith("--"));
  if (!args.includes("--force") && existsSync(join(dir, "results", "summary.csv"))) {
    console.log(`${dir}: results exist, use --force to rerun`);
  } else {
    rmSync(join(dir, "results", "parts"), { recursive: true, force: true });
    merge(dir, await runShard(dir));
  }
}
