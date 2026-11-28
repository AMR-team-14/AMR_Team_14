// B2: Case 3 independent searches (6 directions x 21-23 vehicles x 4 starts, equal budget) split by measured
// throughput, plus Case 2 follow-ups (25 vehicles, one-step distance thresholds, team records).
//   node plan2.cjs B2 github=15.2,kaggle=13.1,local=8.5 [budget]
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const L = require('../lib.cjs');
const R = require('../results.cjs');

const [batch, speedArg, budgetArg] = process.argv.slice(2);
const budget = Number(budgetArg || 800);
const speeds = Object.fromEntries(speedArg.split(',').map(s => s.split('=')).map(([k, v]) => [k, +v]));
const S = require('../seeds.cjs');            // team seed plan: public 11-15, held-out blocks inside 1-500
const PUBLIC = S.PUBLIC, SCREEN = S.STAGES.screen;
const tasks = [], reused = [];
const doneKeys = {};
const keys = c => doneKeys[c] || (doneKeys[c] = new Set(R.allResults(c).filter(r => r.status === 'done').map(R.keyOf)));
function evalTask(c, cand, label, hypothesis, start) {
  const key = [c, cand.sha, L.inputHash(c), L.ENGINE, R.seedText(start, 5), L.AGG].join('|');
  if (keys(c).has(key)) { reused.push(label + '@' + start); return; }
  const id = `${batch}:c${c}:${cand.sha.slice(0, 12)}:${start}`;
  if (tasks.some(t => t.id === id)) { reused.push(label + '@' + start + ' (same batch)'); return; }
  tasks.push({id, batch, env: null, type: 'eval', case: c, label, hypothesis, candSha: cand.sha, design: cand.text, start, count: 5, cost: 1});
}
const register = (c, design, label, origin) => { const cand = L.candidate(c, design); R.saveCandidate(c, cand, label, origin); return cand; };

// Case 3 searches.
const dirs = JSON.parse(fs.readFileSync(path.join(L.HERE, 'jobs', 'case3-dirs.json'), 'utf8'));
const base3 = L.caseInput(3).csv;
let k = 0;
for (const {name, dir} of dirs) for (const fleet of [21, 22, 23]) for (const only of ['nearest', 'shortFar', 'longFar', 'transplant']) {
  tasks.push({id: `${batch}:c3:search:${name}-n${fleet}-${only}`, batch, env: null, type: 'search', case: 3, dir, fleet, only,
    transplant: only === 'transplant' ? base3 : undefined, budget, seed: 3000 + k++,
    label: `c3-${name}-n${fleet}-search`, hypothesis: 'C3 independent search per fleet: does the best direction/tree change with 21/22/23 vehicles?',
    cost: budget});
}

// Case 2 follow-ups: 25 vehicles on the public-best and screen-best cells, then one-step distance thresholds.
const c2 = Object.fromEntries(fs.readFileSync(path.join(R.caseDir(2), 'candidates', 'index.csv'), 'utf8').trim().split('\n').slice(1)
  .map(l => R.parseCsv('a,b,c,d,e\n' + l + '\n')[0]).filter(r => /^c2-cell\d:/.test(r.b)).map(r => [r.b.slice(0, 8), r.a]));
const designOf = sha => L.parseDesign(fs.readFileSync(path.join(R.caseDir(2), 'candidates', sha.slice(0, 12) + '.csv'), 'utf8'));
const hyp2 = 'C2 follow-up: 25 vehicles and one-step distance thresholds on the public-best (cell0) and screen-best (cell6) combinations';
for (const [cell, tag] of [['c2-cell0', 'pubBest'], ['c2-cell6', 'screenBest']]) {
  const d = designOf(c2[cell]);
  const variants = [{...d, fleet: 25, tag: `${cell}-n25`}];
  for (const fleet of [d.fleet, 25]) {
    const g = L.geometry(2, d.dir);
    d.nodes.forEach((n, i) => {
      if (n.f !== 'de' && n.f !== 'dl') return;
      const V = g[n.f];
      const kk = V.filter(v => v <= n.th).length;     // values on the "<= threshold" side
      for (const [step, idx] of [['down', kk - 2], ['up', kk]]) {
        if (idx < 0 || idx >= V.length) continue;
        const nodes = d.nodes.map((m, j) => j === i ? {...m, th: V[idx]} : {...m});
        variants.push({...d, fleet, nodes, tag: `${cell}-n${fleet}-node${n.id}${n.f}-${step}`});
      }
    });
  }
  for (const v of variants) {
    const cand = register(2, v, `${v.tag} (${tag})`, hyp2);
    for (const s of [PUBLIC, ...SCREEN]) evalTask(2, cand, v.tag, hyp2, s);
  }
}
// Case 2 team records re-evaluated with the common evaluator.
for (const f of fs.readdirSync(path.join(L.HERE, 'team', 'case2'))) {
  const [who, name] = f.split('__');
  const cand = register(2, L.parseDesign(fs.readFileSync(path.join(L.HERE, 'team', 'case2', f), 'utf8')), `${who}:${name}`, `jinhong-experiments@98597c1:jinhong/case2/${name}`);
  for (const s of [PUBLIC, ...SCREEN]) evalTask(2, cand, `${who}:${name}`, 'C2 team record re-evaluated with common evaluator', s);
}

// Assign: largest tasks first to the environment with the lowest projected finish time (cost / speed).
const load = Object.fromEntries(Object.keys(speeds).map(e => [e, 0]));
for (const t of [...tasks].sort((a, b) => b.cost - a.cost)) {
  const env = Object.keys(speeds).sort((a, b) => (load[a] + t.cost) / speeds[a] - (load[b] + t.cost) / speeds[b])[0];
  t.env = env; load[env] += t.cost;
}
fs.writeFileSync(path.join(L.HERE, 'jobs', batch + '.jsonl'), tasks.map(({cost, ...t}) => JSON.stringify(t)).join('\n') + '\n');
const by = {};
for (const t of tasks) { const b = by[t.env] || (by[t.env] = {search: 0, eval: 0}); b[t.type]++; }
console.log(batch, tasks.length, JSON.stringify(by), 'projected seconds', JSON.stringify(Object.fromEntries(Object.entries(load).map(([e, l]) => [e, Math.round(l / speeds[e])]))), 'reused', reused.length, reused.join(', '));
