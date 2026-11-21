// Overnight driver for the JSM U/W searches. Runs locally; the experiments themselves run on
// GitHub Actions. Each round: pull, unlock the next stage, grow every family, push, wait.
//   node tools/orchestrate.mjs --until 2026-11-22T12:30:00+09:00
import { execFileSync } from "node:child_process";
import { existsSync, readdirSync, readFileSync, writeFileSync, appendFileSync } from "node:fs";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { gunzipSync } from "node:zlib";

const root = fileURLToPath(new URL("..", import.meta.url));
const args = process.argv.slice(2);
const until = new Date(args[args.indexOf("--until") + 1] ?? Date.now() + 12 * 3600e3);
const branch = "codex/simulator-reproducibility";
const home = join(root, "experiments", "JSM");
const log = join(home, "orchestrator.log");
const sh = (cmd, a) => execFileSync(cmd, a, { cwd: root, encoding: "utf8", maxBuffer: 1 << 28, stdio: ["ignore", "pipe", "ignore"] }).trim();
const note = m => { const line = `${new Date().toISOString()} ${m}`; console.log(line); appendFileSync(log, line + "\n"); };
const sleep = ms => new Promise(r => setTimeout(r, ms));
const mean = xs => xs.reduce((a, b) => a + b, 0) / xs.length;

function rows(dir) {
  return gunzipSync(readFileSync(join(dir, "results", "raw.jsonl.gz"))).toString().split("\n").filter(Boolean).map(l => JSON.parse(l));
}

// After F01: search fleets = contiguous range covering fleets within 30 of each tree's own minimum
// (no rejected seed), for the family root and the 1,185 baseline.
function unlockAfterF01() {
  const f01 = join(root, "experiments", "common", "F01-fleet-survey");
  if (!existsSync(join(f01, "results", "summary.csv"))) return false;
  const all = rows(f01);
  let changed = false;
  for (const [id, rootTree] of [["U01-layer2", "U0.csv"], ["W01-root-threshold", "W0.csv"]]) {
    const runPath = join(home, id, "run.json");
    const spec = JSON.parse(readFileSync(runPath, "utf8"));
    if (spec.status !== "planned") continue;
    const fleets = new Set();
    for (const tree of [rootTree, "B1185.csv"]) {
      const ok = all.filter(r => r.tree === tree && r.cost.every(c => c !== null)).map(r => ({ f: r.fleet, avg: mean(r.cost) }));
      const min = Math.min(...ok.map(o => o.avg));
      ok.filter(o => o.avg <= min + 30).forEach(o => fleets.add(o.f));
    }
    const lo = Math.min(...fleets), hi = Math.max(...fleets);
    spec.fleets = Array.from({ length: hi - lo + 1 }, (_, i) => lo + i);
    spec.status = "ready";
    writeFileSync(runPath, JSON.stringify(spec, null, 2) + "\n");
    note(`${id}: fleets ${lo}-${hi}, ready`);
    changed = true;
  }
  return changed;
}

function activeRun() {
  const out = sh("gh", ["run", "list", "--branch", branch, "--workflow", "experiments.yml", "-L", "5", "--json", "status,databaseId,conclusion"]);
  const runs = JSON.parse(out);
  const failed = runs.find(r => r.status === "completed" && r.conclusion === "failure");
  return { busy: runs.some(r => r.status !== "completed"), lastFailed: runs[0]?.conclusion === "failure" ? runs[0].databaseId : null, failed };
}

while (true) {
  sh("git", ["pull", "-q", "--ff-only", "origin", branch]);
  const { busy, lastFailed } = activeRun();
  if (lastFailed) { note(`workflow run ${lastFailed} failed; stopping for a human`); break; }
  if (busy) { await sleep(60e3); continue; }

  let changed = unlockAfterF01();
  const states = readdirSync(home).filter(f => /^search-.*\.json$/.test(f)).map(f => join(home, f));
  if (Date.now() < until.getTime()) {
    for (const s of states) {
      const before = readFileSync(s, "utf8");
      note(sh("node", ["tools/grow.mjs", s]));
      if (readFileSync(s, "utf8") !== before) changed = true;
    }
  }
  if (changed) {
    sh("git", ["add", "experiments"]);
    sh("git", ["commit", "-q", "-m", "Grow U/W search layers", "-m", "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"]);
    sh("git", ["push", "-q", "origin", branch]);
    note("pushed next layers");
    await sleep(90e3);
    continue;
  }
  const done = states.every(s => JSON.parse(readFileSync(s, "utf8")).done);
  if (done) { note("all families done"); break; }
  if (Date.now() >= until.getTime()) { note("deadline reached; no new layers"); break; }
  await sleep(60e3);
}
