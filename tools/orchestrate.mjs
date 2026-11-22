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
const arg = (k, d) => args.includes(k) ? args[args.indexOf(k) + 1] : d;
const until = new Date(arg("--until", Date.now() + 12 * 3600e3));
// Round 2: --families D,L,Q,UG,WG,LG,QG --post post-R2.json --xc X03-fresh --xd X04-final --seeds-c 4201-4400 --seeds-d 5001-6000
const families = arg("--families", null)?.split(",");
// New growth layers stop at --grow-until (default --until); follow-up stages may start until --until.
const growUntil = new Date(arg("--grow-until", until));
const postName = arg("--post", "post.json");
const XC = arg("--xc", "X01-fresh"), XD = arg("--xd", "X02-final");
const stageEnv = { ...process.env, STAGE_XC: XC, STAGE_XD: XD, SEEDS_C: arg("--seeds-c", "2001-2200"), SEEDS_D: arg("--seeds-d", "3001-4000") };
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
    if (!existsSync(runPath)) continue;
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

// Best non-rejected row of an experiment's summary.csv, by tree.
function summaryRows(dir) {
  const f = join(dir, "results", "summary.csv");
  if (!existsSync(f)) return null;
  const [head, ...lines] = readFileSync(f, "utf8").trim().split(/\r?\n/);
  const keys = head.split(",");
  return lines.map(l => {
    const cells = l.match(/("[^"]*"|[^,]*)(,|$)/g).map(c => c.replace(/,$/, "").replace(/^"|"$/g, ""));
    return Object.fromEntries(keys.map((k, i) => [k, cells[i]]));
  }).filter(r => r.average !== "" && +r.rejected === 0);
}
function bestByTree(rows) {
  const m = new Map();
  for (const r of rows) if (!m.has(r.notation) || +r.average < +m.get(r.notation).average) m.set(r.notation, r);
  return [...m.values()].sort((a, b) => +a.average - +b.average);
}
const code = t => "`" + t + "`";
function writeReport(status) {
  const kst = d => new Date(d).toLocaleString("ko-KR", { timeZone: "Asia/Seoul", hour12: false });
  const out = ["# 루트별 탐색 현황", "", `- 갱신: ${kst(Date.now())} (KST) · 30분마다 자동 갱신`, `- 상태: ${status}`, `- 새 층 생성 마감: ${kst(growUntil)} · 후속 단계 시작 마감: ${kst(until)}`,
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
  // Follow-up stages: best tree of each experiment, and paired difference to the 1,185 tree when it is inside.
  const vs1185 = dir => {
    if (!existsSync(join(dir, "results", "raw.jsonl.gz"))) return null;
    const all = rows(dir).filter(r => r.cost.every(c => c !== null));
    const refs = all.filter(r => r.tree === "ref_B1185.csv").sort((a, b) => mean(a.cost) - mean(b.cost));
    const fam = all.filter(r => !/^ref_(B1185|D0)/.test(r.tree)).sort((a, b) => mean(a.cost) - mean(b.cost));
    if (!fam.length) return null;
    const b = fam[0], res = { b, avg: mean(b.cost), sd: Math.sqrt(b.cost.reduce((a, x) => a + (x - mean(b.cost)) ** 2, 0) / (b.cost.length - 1)) };
    if (refs.length) {
      const d = b.cost.map((c, i) => c - refs[0].cost[i]), m = mean(d);
      const se = Math.sqrt(d.reduce((a, x) => a + (x - m) ** 2, 0) / (d.length - 1) / d.length);
      Object.assign(res, { d: m, lcb: m - 1.96 * se, refFleet: refs[0].fleet });
    }
    return res;
  };
  const stageNames = { A: "A 짝 확장", A2: "A2 확인(1065~1264)", B: "B 방법 A 이웃", B2: "B2 확인(1065~1264)" };
  const stageRows = [];
  const fmtRow = (label, dir) => {
    const r = vs1185(join(home, dir));
    return `| ${label} | ${dir} | ${r ? code(r.b.notation) : "실행 대기·중"} | ${r?.b.fleet ?? ""} | ${r ? r.avg.toFixed(2) : ""} | ${r ? r.sd.toFixed(1) : ""} | ${r?.d !== undefined ? `${r.d.toFixed(2)} (하한 ${r.lcb.toFixed(2)}, 1185안 ${r.refFleet}대)` : ""} |`;
  };
  for (const f of readdirSync(home).filter(f => /^search-.*\.json$/.test(f))) {
    const st = JSON.parse(readFileSync(join(home, f), "utf8"));
    for (const [k, label] of Object.entries(stageNames)) if (st.stages?.[k]) stageRows.push(fmtRow(`${st.family} ${label}`, st.stages[k]));
  }
  for (const [dir, label] of [["X01-fresh", "U·W C 보류 시드(2001~2200)"], ["X02-final", "U·W D 새 시드(3001~4000)"],
    [XC, `2차 C 보류 시드(${stageEnv.SEEDS_C})`], [XD, `2차 D 새 시드(${stageEnv.SEEDS_D})`]].filter((x, i, a) => a.findIndex(y => y[0] === x[0]) === i))
    if (existsSync(join(home, dir))) stageRows.push(fmtRow(label, dir));
  if (stageRows.length) out.push("## 후속 단계 (팀 방법 A 절차 적용)", "",
    "| 단계 | 실험 | 최선 트리 | 대수 | 평균 | 표준편차 | 1,185안 대비 차이 |", "| --- | --- | --- | ---: | ---: | ---: | --- |", ...stageRows,
    "", "차이는 같은 시드 짝비교(음수면 1,185안보다 쌈). 하한은 단측이 아닌 95% 신뢰구간 하한.", "");
  for (const [xc, xd] of [...new Set(["X01-fresh|X02-final", `${XC}|${XD}`])].map(x => x.split("|")).reverse()) {
  const x01 = summaryRows(join(home, xc));
  if (x01) out.push(`### ${xc} C 단계 전체 (트리별 최적 대수)`, "", "| 트리 | 최적 대수 | 평균 | 표준편차 | 나쁜 쪽 5% |", "| --- | ---: | ---: | ---: | ---: |",
    ...bestByTree(x01).slice(0, 30).map(r => `| ${code(r.notation)} | ${r.fleet} | ${(+r.average).toFixed(2)} | ${(+r.costSD).toFixed(1)} | ${(+r.costP95).toFixed(1)} |`), "");
  const x02 = summaryRows(join(home, xd));
  if (x02) out.push(`### ${xd} D 단계 (새 시드)`, "", "| 트리 | 대수 | 평균 | 표준편차 | 나쁜 쪽 5% |", "| --- | ---: | ---: | ---: | ---: |",
    ...x02.sort((a, b) => +a.average - +b.average).map(r => `| ${code(r.notation)} | ${r.fleet} | ${(+r.average).toFixed(2)} | ${(+r.costSD).toFixed(1)} | ${(+r.costP95).toFixed(1)} |`), "");
  }
  out.push("## F01 대수 조사 (루트 하나짜리)", "", ...f01Table(), "");
  const notes = join(home, "report-notes.md");
  if (existsSync(notes)) out.push("## 판단과 조정 제안", "", readFileSync(notes, "utf8"));
  writeFileSync(reportPath, out.join("\n") + "\n");
}
// Other workflows (GP) also push to the branch: rebase before pushing, retry a few times.
function push() {
  for (let i = 0; ; i++) {
    try { sh("git", ["pull", "-q", "--rebase", "--autostash", "origin", branch]); sh("git", ["push", "-q", "origin", branch]); return; }
    catch (e) { if (i >= 4) throw e; execFileSync("node", ["-e", "setTimeout(()=>{},20000)"]); }
  }
}
function pushReport(status) {
  lastReport = Date.now();
  writeReport(status);
  sh("git", ["add", "experiments/JSM/REPORT.md", "experiments/JSM/orchestrator.log"].concat(existsSync(join(home, "report-notes.md")) ? ["experiments/JSM/report-notes.md"] : []));
  try { sh("git", ["commit", "-q", "-m", "Update U/W search report [skip ci]"]); } catch { return; }
  push();
  lastReport = Date.now();
}

while (true) {
  try { sh("git", ["pull", "-q", "--ff-only", "origin", branch]); } catch { note("git pull failed; retrying in 60 s"); await sleep(60e3); continue; }
  const { busy, lastFailed } = activeRun();
  if (Date.now() - lastReport >= 30 * 60e3) pushReport(busy ? "GitHub에서 실험 실행 중" : "다음 층 준비 또는 대기");
  if (lastFailed) { note(`workflow run ${lastFailed} failed; stopping for a human`); pushReport(`워크플로 실패(${lastFailed}), 사람 확인 필요`); break; }
  if (busy) { await sleep(60e3); continue; }

  let changed = unlockAfterF01();
  const states = readdirSync(home).filter(f => /^search-.*\.json$/.test(f))
    .filter(f => !families || families.includes(f.slice(7, -5))).map(f => join(home, f));
  if (Date.now() < growUntil.getTime()) {
    for (const s of states) {
      const before = readFileSync(s, "utf8");
      note(sh("node", ["tools/grow.mjs", s]));
      if (readFileSync(s, "utf8") !== before) changed = true;
    }
  }
  if (changed) {
    sh("git", ["add", "experiments"]);
    sh("git", ["commit", "-q", "-m", "Grow search layers", "-m", "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"]);
    push();
    note("pushed next layers");
    await sleep(90e3);
    continue;
  }
  if (Date.now() >= growUntil.getTime())
    for (const s of states) {
      const st = JSON.parse(readFileSync(s, "utf8"));
      const last = st.layers.at(-1);
      if (!st.done && st.ref && (!last || existsSync(join(home, last, "results", "summary.csv")) || st.method !== "experiment")) {
        st.done = true; st.stoppedAtDeadline = true; writeFileSync(s, JSON.stringify(st, null, 2) + "\n");
        note(`${st.family}: growth deadline, stopping with ${st.layers.length} layers`);
      }
    }
  const done = states.every(s => JSON.parse(readFileSync(s, "utf8")).done);
  if (done) {
    // Follow-up stages (tools/stages.mjs): A -> A2 -> B -> B2 -> C -> D, each after the previous finished.
    const postPath = join(home, postName);
    const post = existsSync(postPath) ? JSON.parse(readFileSync(postPath, "utf8")) : { stage: "start" };
    const finished = dirs => dirs.every(d => existsSync(join(home, d, "results", "summary.csv")));
    const stageDirs = stage => stage === "C" ? [XC] : stage === "D" ? [XD]
      : states.map(s => JSON.parse(readFileSync(s, "utf8")).stages?.[stage]).filter(Boolean);
    const order = ["start", "A", "A2", "B", "B2", "C", "D"];
    if (post.stage === "D" && finished(stageDirs("D"))) { post.stage = "done"; writeFileSync(postPath, JSON.stringify(post) + "\n"); note("stage D done"); pushReport("후속 단계 A~D 모두 종료"); break; }
    if (post.stage === "done") { pushReport("후속 단계 A~D 모두 종료"); break; }
    const next = order[order.indexOf(post.stage) + 1];
    if (next && (post.stage === "start" || finished(stageDirs(post.stage))) && Date.now() < until.getTime()) {
      note(execFileSync("node", ["tools/stages.mjs", next, ...states], { cwd: root, encoding: "utf8", env: stageEnv }).trim());
      post.stage = next; writeFileSync(postPath, JSON.stringify(post) + "\n");
      sh("git", ["add", "experiments"]);
      sh("git", ["commit", "-q", "-m", `Start follow-up stage ${next}`, "-m", "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"]);
      push();
      note(`pushed stage ${next}`);
      await sleep(90e3);
      continue;
    }
  }
  if (Date.now() >= until.getTime()) { note("deadline reached; no new layers"); pushReport("마감 도달, 새 층 생성 중단"); break; }
  await sleep(60e3);
}
