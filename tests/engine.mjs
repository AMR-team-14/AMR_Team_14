import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import createEngine from "../docs/engine/agv.mjs";

const root = fileURLToPath(new URL("..", import.meta.url));
const contents = path => readFileSync(new URL("../" + path, import.meta.url), "utf8");
const native = fileURLToPath(new URL("../build/agv_opt" +
  (process.platform === "win32" ? ".exe" : ""), import.meta.url));
function cli(...args) {
  const run = spawnSync(native, args, {cwd:root, encoding:"utf8"});
  if (run.error) throw run.error;
  return run;
}

const engine = await createEngine({
  wasmBinary: readFileSync(new URL("../docs/engine/agv.wasm", import.meta.url))
});
const initialize = engine.cwrap("agv_initialize", "string", ["string", "string"]);
const evaluate = engine.cwrap("agv_evaluate", "string", ["string"]);
assert.deepEqual(JSON.parse(initialize(contents("docs/case.json"), contents("docs/seed11.json"))), {});

const fixtures = {
  "answer_final_best.csv": [1183.5078, 1194.2396, 1207.3680, 1208.2963, 1197.4968],
  "robust_best_18.csv": [1188.7926, 1184.8815, 1211.0476, 1213.1166, 1200.9634],
  "robust_best_19.csv": [1183.5078, 1194.2396, 1207.3680, 1208.2963, 1197.4968],
  "robust_best_20.csv": [1179.8209, 1195.3046, 1207.7404, 1204.4888, 1211.2004]
};
for (const [file, baseline] of Object.entries(fixtures)) {
  const candidate = "solutions/KMJ/" + file;
  const browser = JSON.parse(evaluate(contents(candidate)));
  const command = cli("--eval-json", candidate);
  assert.equal(command.status, 0, command.stderr);
  const cliScore = JSON.parse(command.stdout);
  assert.deepEqual(browser, cliScore, file);
  assert.deepEqual(cliScore.scores.map(row=>row.seed), [11,12,13,14,15]);
  assert.deepEqual(cliScore.scores.map(row=>Number(row.cost.toFixed(4))), baseline, file);
  console.log(file + " | " + cliScore.average.toFixed(4));
}

const holdout = cli("--eval-json", "solutions/KMJ/answer_final_best.csv", "--seeds", "11,151,152");
assert.equal(holdout.status,0,holdout.stderr);
assert.deepEqual(JSON.parse(holdout.stdout).scores.map(row=>row.seed), [11,151,152]);
const dir = JSON.parse(contents("docs/case.json")).fixedDir;
const congested = JSON.parse(evaluate("dir,"+dir+"\nfleet,1\nkey,1,de,min,65,0,0"));
assert(congested.scores.every(row=>row.rejected && row.cost === null));
assert.equal(congested.average,null);
assert.match(JSON.parse(evaluate("dir,"+dir+"\nfleet,19\nkey,1,de,min,65,1,0")).error, /invalid tree/i);
assert.match(JSON.parse(evaluate("dir,0\nfleet,19\nkey,1,de,min,65,0,0")).error, /direction/i);
console.log("Original scores, native/WASM parity, held-out seeds, and policy rejection passed.");
