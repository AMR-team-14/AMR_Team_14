// Run one experiment folder: every tree in trees/ x every fleet in run.json, on the same seeds.
//   node tools/run-batch.mjs experiments/<owner>/<id> [--force]
//   node tools/run-batch.mjs --pending      runs every run.json with "status": "ready" and no results
import { execFile, execFileSync } from "node:child_process";
import { existsSync, mkdirSync, readdirSync, readFileSync, writeFileSync } from "node:fs";
import { cpus } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { promisify } from "node:util";
import { readTree, treeText, notation, seedList } from "./tree-lib.mjs";

const run = promisify(execFile);
const root = fileURLToPath(new URL("..", import.meta.url));
const binary = join(root, "build", process.platform === "win32" ? "agv_opt.exe" : "agv_opt");
const fixedDir = JSON.parse(readFileSync(join(root, "docs/case.json"), "utf8")).fixedDir;
const args = process.argv.slice(2);

function experimentDirs() {
  if (!args.includes("--pending")) return [args.find(a => !a.startsWith("--"))];
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
  return found;
}

async function pool(items, limit, task) {
  const out = new Array(items.length);
  let next = 0;
  await Promise.all(Array.from({ length: limit }, async () => {
    while (next < items.length) { const i = next++; out[i] = await task(items[i]); }
  }));
  return out;
}

const mean = xs => xs.reduce((a, b) => a + b, 0) / xs.length;
const se = xs => xs.length < 2 ? 0 : Math.sqrt(xs.reduce((a, x) => a + (x - mean(xs)) ** 2, 0) / (xs.length - 1) / xs.length);
const fmt = x => x === null || Number.isNaN(x) ? "" : Number(x.toFixed(4));

async function runExperiment(dir) {
  const spec = JSON.parse(readFileSync(join(dir, "run.json"), "utf8"));
  const seeds = seedList(spec.seeds);
  const treeDir = join(dir, spec.trees ?? "trees");
  const trees = readdirSync(treeDir).filter(f => f.endsWith(".csv") && !f.startsWith("index_")).sort();
  const resultDir = join(dir, "results");
  const scratch = join(resultDir, ".csv");
  mkdirSync(scratch, { recursive: true });
  const jobs = trees.flatMap(tree => spec.fleets.map(fleet => ({ tree, fleet })));
  const started = Date.now();
  const rows = await pool(jobs, cpus().length, async ({ tree, fleet }) => {
    const nodes = readTree(join(treeDir, tree));
    const csv = join(scratch, `${tree.replace(/\.csv$/, "")}-n${fleet}.csv`);
    writeFileSync(csv, `dir,${fixedDir}\nfleet,${fleet}\n${treeText(nodes)}`);
    const { stdout } = await run(binary, ["--eval-json", csv, "--seeds", seeds.join(",")], { maxBuffer: 1 << 26 });
    return { tree, notation: notation(nodes), fleet, ...JSON.parse(stdout) };
  });
  writeFileSync(join(resultDir, "raw.jsonl"), rows.map(r => JSON.stringify(r)).join("\n") + "\n");

  const base = spec.baseline && new Map(rows.filter(r => r.tree === spec.baseline).map(r => [r.fleet, r]));
  const header = "tree,notation,fleet,rejected,average,avgWait,urgent95,maxQueue,endQueueMax,deltaVsBaseline,deltaSE";
  const lines = rows.map(r => {
    const ok = r.scores.filter(s => !s.rejected);
    const rejected = r.scores.length - ok.length;
    let delta = null, deltaSE = null;
    const b = base?.get(r.fleet);
    if (b && r.average !== null && b.average !== null) {
      const d = r.scores.map((s, i) => b.scores[i].cost - s.cost);   // + means cheaper than baseline
      delta = mean(d); deltaSE = se(d);
    }
    return [r.tree, `"${r.notation}"`, r.fleet, rejected, fmt(r.average),
      fmt(ok.length ? mean(ok.map(s => s.averageWait)) : null), fmt(ok.length ? mean(ok.map(s => s.urgent95)) : null),
      Math.max(...r.scores.map(s => s.maxQueue)), Math.max(...r.scores.map(s => s.endQueue)), fmt(delta), fmt(deltaSE)].join(",");
  });
  writeFileSync(join(resultDir, "summary.csv"), [header, ...lines].join("\n") + "\n");
  const commit = execFileSync("git", ["rev-parse", "--short", "HEAD"], { cwd: root }).toString().trim();
  writeFileSync(join(resultDir, "meta.json"), JSON.stringify({
    engineCommit: commit, seeds, fleets: spec.fleets, trees: trees.length, evaluations: jobs.length,
    runner: process.env.GITHUB_ACTIONS ? "github-actions" : "local", threads: cpus().length,
    seconds: Math.round((Date.now() - started) / 1000), finishedAt: new Date().toISOString()
  }, null, 2) + "\n");
  console.log(`${dir}: ${jobs.length} evaluations in ${Math.round((Date.now() - started) / 1000)} s`);
}

const dirs = experimentDirs();
if (!dirs.length || !dirs[0]) console.log("No experiment to run.");
for (const dir of dirs) {
  if (!args.includes("--force") && existsSync(join(dir, "results", "summary.csv"))) {
    console.log(`${dir}: results exist, use --force to rerun`); continue;
  }
  await runExperiment(dir);
}
