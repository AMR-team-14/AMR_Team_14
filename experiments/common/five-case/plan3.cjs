// Evaluate listed candidates on a seed stage, split by throughput. Skips blocks already done with the same key.
//   node plan3.cjs <batch> <stage: screen|confirm|final> <speeds> <case>:<sha12>[=label] ...
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const L = require('./lib.cjs');
const R = require('./results.cjs');

const [batch, stage, speedArg, ...items] = process.argv.slice(2);
const {STAGES} = require('./seeds.cjs');      // team seed plan: held-out blocks inside 1-500 (screen ⊂ confirm ⊂ final)
const starts = STAGES[stage];
if (!starts) throw Error('stage?');
const speeds = Object.fromEntries(speedArg.split(',').map(s => s.split('=')).map(([k, v]) => [k, +v]));
const old = fs.existsSync(path.join(L.HERE, 'jobs', batch + '.jsonl'))
  ? fs.readFileSync(path.join(L.HERE, 'jobs', batch + '.jsonl'), 'utf8').split(/\r?\n/).filter(Boolean).map(JSON.parse) : [];
const tasks = [...old];
let reused = 0;
const doneKeys = {};
const keys = c => doneKeys[c] || (doneKeys[c] = new Set(R.allResults(c).filter(r => r.status === 'done').map(R.keyOf)));
for (const item of items) {
  const [ref, label] = item.split('=');
  const [c, sha12] = ref.split(':').map((x, i) => i ? x : +x);
  const text = fs.readFileSync(path.join(R.caseDir(c), 'candidates', sha12 + '.csv'), 'utf8');
  const cand = L.candidate(c, L.parseDesign(text));
  const name = label || sha12;
  for (const s of starts) {
    const key = [c, cand.sha, L.inputHash(c), L.ENGINE, R.seedText(s, 5), L.AGG].join('|');
    if (keys(c).has(key)) { reused++; continue; }
    const id = `${batch}:c${c}:${cand.sha.slice(0, 12)}:${s}`;
    if (tasks.some(t => t.id === id)) continue;
    tasks.push({id, batch, env: null, type: 'eval', case: c, label: name, hypothesis: `${stage} stage on held-out seeds (paired with baseline)`,
      candSha: cand.sha, design: cand.text, start: s, count: 5});
  }
  // final stage also records the pooled 1-500 cost, the number teammates report, so every answer is comparable
  if (stage === 'final') {
    const {POOLED} = require('./seeds.cjs');
    const id = `${batch}:c${c}:${cand.sha.slice(0, 12)}:pooled1-500`;
    if (!tasks.some(t => t.id === id)) tasks.push({id, batch, env: null, type: 'eval', case: c, label: name,
      hypothesis: 'pooled 1-500 (team comparison)', candSha: cand.sha, design: cand.text, start: POOLED.start, count: POOLED.count});
  }
}
// Round-robin weighted by throughput so every environment gets a share of every candidate.
const envs = Object.keys(speeds);
const credit = Object.fromEntries(envs.map(e => [e, 0]));
for (const t of tasks) {
  if (t.env) continue;
  for (const e of envs) credit[e] += speeds[e];
  const e = envs.sort((a, b) => credit[b] - credit[a])[0];
  t.env = e; credit[e] -= envs.reduce((s, x) => s + speeds[x], 0);
}
fs.writeFileSync(path.join(L.HERE, 'jobs', batch + '.jsonl'), tasks.map(t => JSON.stringify(t)).join('\n') + '\n');
const by = {};
for (const t of tasks) by[t.env] = (by[t.env] || 0) + 1;
console.log(batch, stage, 'tasks', tasks.length, JSON.stringify(by), 'reused blocks', reused);
