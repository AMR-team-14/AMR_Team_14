// Check one batch and write experiments/caseN/JSM-<batch>.csv.
//   node collect.cjs <batch> <env>=<raw.jsonl>@<location> ...
// Checks: every planned task returned exactly once from its assigned environment, no failures,
// and no key (case, candidate, input, evaluator, seeds, aggregation) with two different scores.
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const L = require('./lib.cjs');
const R = require('./results.cjs');

const [batch, ...srcs] = process.argv.slice(2);
const tasks = fs.readFileSync(path.join(L.HERE, 'jobs', batch + '.jsonl'), 'utf8').split(/\r?\n/).filter(Boolean).map(JSON.parse);
const byId = new Map(tasks.map(t => [t.id, t]));
const REPORTED = JSON.parse(fs.readFileSync(path.join(L.HERE, 'team', 'reported.json'), 'utf8'));
const rawDir = path.join(L.HERE, 'raw');
fs.mkdirSync(rawDir, {recursive: true});

const got = new Map(), problems = [], speed = {};
for (const s of srcs) {
  const [env, rest] = s.split('=');
  const [file, ...loc] = rest.split('@');
  const location = loc.join('@');
  const text = fs.readFileSync(file, 'utf8');
  const keep = `experiments/JSM/five-case/raw/${batch}-${env}.jsonl`;
  fs.writeFileSync(path.join(R.REPO, keep), text.replace(/\r\n/g, '\n'));
  const envInfo = path.join(path.dirname(file), `${batch}-${env}.env.json`);
  if (fs.existsSync(envInfo)) fs.copyFileSync(envInfo, path.join(rawDir, `${batch}-${env}.env.json`));
  let ms = 0, n = 0, evals = 0;
  for (const line of text.split(/\r?\n/).filter(Boolean)) {
    const r = JSON.parse(line);
    const t = byId.get(r.id);
    if (!t) { problems.push(`unknown task ${r.id} from ${env}`); continue; }
    if (t.env !== env) { problems.push(`task ${r.id} assigned to ${t.env} but returned by ${env}`); continue; }
    if (got.has(r.id)) {
      const prev = got.get(r.id);
      if (prev.out.status === 'done' && r.out.status === 'done' && prev.out.score !== r.out.score) problems.push(`duplicate ${r.id} with different scores`);
      if (r.out.status !== 'done') continue;          // keep the completed attempt
    }
    got.set(r.id, {...r, location, keep});
    ms += r.ms; n++; evals += t.type === 'search' ? (r.out.evals || 0) : 1;
  }
  speed[env] = {tasks: n, cpuSeconds: +(ms / 1000).toFixed(1), msPerEval: evals ? +(ms / evals).toFixed(1) : null, evals};
}
const missing = tasks.filter(t => !got.has(t.id)).map(t => t.id);
const failed = [...got.values()].filter(r => r.out.status !== 'done').map(r => `${r.id}: ${r.out.error}`);

const rows = {};
const add = (c, row) => (rows[c] || (rows[c] = [])).push(row);
function baseRow(t, cand, r) {
  return {case: t.case, author: 'JSM', batch, candidate: t.label, fleet: cand.design.fleet, dir: cand.design.dir,
    tree: L.treeText(cand.design.nodes), candidate_path: R.saveCandidate(t.case, cand, t.type === 'search' ? t.label : null, `search ${batch}`),
    candidate_sha256: cand.sha, input_sha256: L.inputHash(t.case), evaluator: L.ENGINE, aggregation: L.AGG,
    env: r.env, raw_log: `${r.keep} (${r.location})`};
}
const scoreFields = o => o.score === null || o.score === undefined ? {score: '', rejected: o.rejected && o.rejected.length ? 1 : 0} : {
  score: o.score.toFixed(4), vehicle_cost: o.veh.toFixed(2), wait_cost: o.wc.toFixed(4), urgent_cost: o.uc.toFixed(4),
  mean_wait_min: o.mean.toFixed(4), urgent_p95_min: o.u95.toFixed(4), rejected: 0};
for (const r of got.values()) {
  const t = byId.get(r.id);
  if (t.type === 'eval') {
    const cand = L.candidate(t.case, L.parseDesign(t.design));
    const o = r.out;
    const rep = REPORTED[`${t.case}:${t.label}`];
    let match = t.crossEnv ? 'cross-environment' : '';
    if (rep && t.start === 11) match = `교차검증: reported ${rep.score} (${rep.how})` + (o.score !== null && o.score.toFixed(2) === String(rep.score) && rep.same ? ' = same' : '');
    add(t.case, {...baseRow(t, cand, r), hypothesis: t.hypothesis, seeds: R.seedText(t.start, t.count), ...scoreFields(o),
      rejected_seeds: (o.rejected || []).join(' '), status: o.status, match, elapsed_ms: r.ms, note: o.error || ''});
  } else {
    for (const s of r.out.results || []) {
      const cand = L.candidate(t.case, L.parseDesign(s.text));
      const label = `${t.label}-from-${s.start}`;
      add(t.case, {...baseRow({...t, label}, cand, r), hypothesis: t.hypothesis, seeds: '11-15', score: Number.isFinite(s.score) ? s.score.toFixed(4) : '',
        rejected: Number.isFinite(s.score) ? 0 : 1, status: 'done', match: 'search-objective', elapsed_ms: r.ms,
        note: `search best from start ${s.start}; ${s.evals} evals; ${s.steps.length} accepted moves; local optimum ${s.localOptimum}`});
    }
  }
}

// Conflicts against this batch and every earlier result file of the same case.
for (const c of Object.keys(rows)) {
  const seen = new Map();
  for (const r of [...R.allResults(c).filter(x => x._file !== `JSM-${batch}.csv`), ...rows[c]]) {
    if (r.status !== 'done') continue;
    const k = R.keyOf(r);
    if (seen.has(k) && seen.get(k) !== r.score) problems.push(`conflict case ${c} ${r.candidate} ${r.seeds}: ${seen.get(k)} vs ${r.score} (${r.env})`);
    seen.set(k, r.score);
  }
  rows[c].sort((a, b) => (a.candidate < b.candidate ? -1 : a.candidate > b.candidate ? 1 : 0) || (a.seeds < b.seeds ? -1 : 1) || (a.env < b.env ? -1 : 1));
  fs.writeFileSync(path.join(R.caseDir(c), `JSM-${batch}.csv`), R.toCsv(rows[c]));
}
const report = {batch, planned: tasks.length, returned: got.size, missing, failed, problems, speed,
  rows: Object.fromEntries(Object.entries(rows).map(([c, r]) => [c, r.length]))};
fs.writeFileSync(path.join(rawDir, `${batch}-check.json`), JSON.stringify(report, null, 1));
console.log(JSON.stringify(report, null, 1));
if (missing.length || failed.length || problems.length) process.exitCode = 2;
