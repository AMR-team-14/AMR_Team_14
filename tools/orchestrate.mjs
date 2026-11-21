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
const branch = "jsm";
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


// experiments/JSM/REPORT.md: progress every 30 minutes, plus report-notes.md (human/Claude judgment).
const reportPath = join(home, "REPORT.md");
let lastReport = 0;
function f01Table() {
  const dir = join(root, "experiments", "common", "F01-fleet-survey");
  if (!existsSync(join(dir, "results", "raw.jsonl.gz"))) return ["F01 결과 없음"];
  const by = new Map();
  for (const r of rows(dir)) {
    if (r.cost.some(c => c === null)) continue;
    const avg = mean(r.cost);
    if (!by.has(r.tree) || avg < by.get(r.tree).avg) by.set(r.tree, { fleet: r.fleet, avg });
  }
  return ["| 트리 | 최적 대수 | 평균 |", "| --- | ---: | ---: |",
    ...[...by].sort((x, y) => x[1].avg - y[1].avg).map(([t, v]) => `| ${t.replace(".csv", "")} | ${v.fleet} | ${v.avg.toFixed(1)} |`)];
}
const code = t => "`" + t + "`";
function writeReport(status) {
  const kst = d => new Date(d).toLocaleString("ko-KR", { timeZone: "Asia/Seoul", hour12: false });
  const out = ["# U·W 탐색 현황", "", `- 갱신: ${kst(Date.now())} (KST) · 30분마다 자동 갱신`, `- 상태: ${status}`, `- 새 층 생성 마감: ${kst(until)}`,
    "- 시드 1001~1064 로컬 평균, 단위 천원/시간. 공식 웹 점수와 다를 수 있음", ""];
  for (const f of readdirSync(home).filter(f => /^search-.*\.json$/.test(f))) {
    const s = JSON.parse(readFileSync(join(home, f), "utf8"));
    out.push(`## ${s.family} 계열 (루트 ${s.root})${s.done ? " · 종료" : ""}`, "");
    if (s.ref) out.push(`- **계열 최선:** ${code(s.ref.notation)} · ${s.ref.fleet}대 · 평균 ${s.ref.avg.toFixed(2)}`);
    out.push(`- 진행 중 층: ${s.done ? "없음" : s.layers.at(-1)}`, "",
      "| 층 | 평가 | 결과 동일로 제외 | 생존 | 층 최선 | 대수 | 평균 | 다음 층 트리 | 상한 |",
      "| --- | ---: | ---: | ---: | --- | ---: | ---: | ---: | --- |");
    for (const h of s.history ?? [])
      out.push(`| ${h.layer} | ${h.evaluated} | ${h.sameAsEarlier ?? ""} | ${h.survivors} | ${code(h.layerBest?.notation ?? "-")} | ${h.layerBest?.fleet ?? ""} | ${h.layerBest?.avg ?? ""} | ${h.nextTrees} | ${h.cappedTo ? h.cappedTo + "개로 축소" : ""} |`);
    out.push("");
  }
  out.push("## F01 대수 조사 (루트 하나짜리)", "", ...f01Table(), "");
  const notes = join(home, "report-notes.md");
  if (existsSync(notes)) out.push("## 판단과 조정 제안", "", readFileSync(notes, "utf8"));
  writeFileSync(reportPath, out.join("\n") + "\n");
}
function pushReport(status) {
  lastReport = Date.now();
  writeReport(status);
  sh("git", ["add", "experiments/JSM/REPORT.md", "experiments/JSM/orchestrator.log"].concat(existsSync(join(home, "report-notes.md")) ? ["experiments/JSM/report-notes.md"] : []));
  try { sh("git", ["commit", "-q", "-m", "Update U/W search report [skip ci]"]); } catch { return; }
  sh("git", ["pull", "-q", "--rebase", "origin", branch]);
  sh("git", ["push", "-q", "origin", branch]);
  lastReport = Date.now();
}

while (true) {
  sh("git", ["pull", "-q", "--ff-only", "origin", branch]);
  const { busy, lastFailed } = activeRun();
  if (Date.now() - lastReport >= 30 * 60e3) pushReport(busy ? "GitHub에서 실험 실행 중" : "다음 층 준비 또는 대기");
  if (lastFailed) { note(`workflow run ${lastFailed} failed; stopping for a human`); pushReport(`워크플로 실패(${lastFailed}), 사람 확인 필요`); break; }
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
  if (done) { note("all families done"); pushReport("두 계열 모두 종료"); break; }
  if (Date.now() >= until.getTime()) { note("deadline reached; no new layers"); pushReport("마감 도달, 새 층 생성 중단"); break; }
  await sleep(60e3);
}
