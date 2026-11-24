// Build one batch of tasks with exactly one environment per (candidate, seed block). Writes jobs/<batch>.jsonl.
//   node plan.cjs B1            first batch (fixed assignment)
//   node plan.cjs B2 <speeds>   later batches split by measured throughput, e.g. github=4.1,kaggle=2.3,local=1.6
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const L = require('./lib.cjs');
const R = require('./results.cjs');
const {startTrees} = require('./search.cjs');

const batch = process.argv[2];
const PUBLIC = 11, SCREEN = [2400001, 2400006, 2400011, 2400016];
const CONFIRM = Array.from({length: 40}, (_, k) => 2410001 + 5 * k);
const FINAL = Array.from({length: 200}, (_, k) => 2420001 + 5 * k);
const ORIGIN = {
  jinhong: 'jinhong-experiments@98597c1:jinhong/case{c}/{f}',
  minjae: 'main@8a384c2:{f}',
};
const tasks = [];
const reused = [];
const done = {};
const doneKeys = c => done[c] || (done[c] = new Set(R.allResults(c).filter(r => r.status === 'done').map(R.keyOf)));

function evalTask(env, c, cand, label, hypothesis, start, extra = {}) {
  const seeds = R.seedText(start, 5);
  const key = [c, cand.sha, L.inputHash(c), L.ENGINE, seeds, L.AGG].join('|');
  if (!extra.forceEnv && doneKeys(c).has(key)) { reused.push({c, label, seeds}); return; }
  const id = [batch, extra.forceEnv ? 'X' + env : '', 'c' + c, cand.sha.slice(0, 12), start].filter(Boolean).join(':');
  if (tasks.some(t => t.id === id)) return;
  tasks.push({id, batch, env, type: 'eval', case: c, label, hypothesis, candSha: cand.sha, design: cand.text, start, count: 5,
    ...(extra.expect ? {expect: extra.expect} : {}), crossEnv: !!extra.forceEnv});
}
const blocks = (env, c, cand, label, hyp, starts, extra) => starts.forEach(s => evalTask(env, c, cand, label, hyp, s, extra));
const register = (c, design, label, origin) => { const cand = L.candidate(c, design); R.saveCandidate(c, cand, label, origin); return cand; };
const baseline = c => register(c, L.parseDesign(L.caseInput(c).csv), `case${c}-baseline`, `inputs.json case ${c} (${L.caseInput(c).path} blob ${L.caseInput(c).sha.slice(0, 7)})`);
function team(c) {
  const dir = path.join(L.HERE, 'team', 'case' + c);
  return fs.readdirSync(dir).filter(f => f.endsWith('.csv')).map(f => {
    const [who, name] = f.split('__');
    const cand = register(c, L.parseDesign(fs.readFileSync(path.join(dir, f), 'utf8')), `${who}:${name}`, ORIGIN[who].replace('{c}', c).replace('{f}', name));
    return {cand, label: `${who}:${name}`};
  });
}

// Case 3 direction set: baseline, D2, and the four lowest flow-weighted loaded distance among the other 52.
function case3Dirs() {
  const base = L.parseDesign(L.caseInput(3).csv).dir, d2 = '1111111100000000011101011001101';
  const ranked = L.directions(3).dirs.filter(d => d !== base && d !== d2)
    .map(d => ({d, s: L.geometry(3, d).sumFD})).sort((a, b) => a.s - b.s || (a.d < b.d ? -1 : 1));
  return [{name: 'B0', dir: base}, {name: 'D2', dir: d2}, ...ranked.slice(0, 4).map((x, i) => ({name: 'F' + (i + 1), dir: x.d, sumFD: x.s}))];
}
const CASE2_DIR26 = '000011111110001110011111101100110101';
const CASE2_TREE26 = ['1,urg,max,0.5,2,0', '2,dl,min,205,0,3', '3,de,max,42.5,0,0'];
function case2Cells() {
  const inc = L.parseDesign(L.caseInput(2).csv);
  const dirs = [inc.dir, CASE2_DIR26];
  const trees = [inc.nodes, L.parseDesign(`dir,${CASE2_DIR26}\nfleet,26\n` + CASE2_TREE26.map(r => 'key,' + r).join('\n')).nodes];
  return Array.from({length: 8}, (_, cell) => {
    const d = Math.floor(cell / 4), t = Math.floor(cell / 2) % 2, f = cell % 2 ? 26 : 24;
    const cand = register(2, {dir: dirs[d], fleet: f, nodes: trees[t]}, `c2-cell${cell}:dir${d}-tree${t}-n${f}`, 'case2 factorial (plan section Case 2)');
    return {cell, d, t, f, cand};
  });
}

if (batch === 'B1') {
  // Cross-environment check: same candidates, same inputs, same five seeds, once per environment.
  const b2 = baseline(2), b3 = baseline(3);
  for (const env of ['local', 'github', 'kaggle']) {
    evalTask(env, 2, b2, 'case2-baseline', 'cross-environment check', PUBLIC, {forceEnv: true, expect: '613.62'});
    evalTask(env, 3, b3, 'case3-baseline', 'cross-environment check', PUBLIC, {forceEnv: true, expect: '962.70'});
    evalTask(env, 3, b3, 'case3-baseline', 'cross-environment check', SCREEN[0], {forceEnv: true});
  }
  // Local: baseline reproduction for all five cases (Case 1 only as a comparison against the stored pilot table).
  const expect = {1: '1180.42', 2: '613.62', 3: '962.70', 4: '801.47', 5: '771.43'};
  for (const c of [1, 2, 3, 4, 5]) evalTask('local', c, baseline(c), `case${c}-baseline`, 'baseline reproduction', PUBLIC, {expect: expect[c]});
  // GitHub: Case 2 factorial, 8 cells x (public + 4 screening blocks) = 40 rows.
  for (const x of case2Cells()) {
    const hyp = 'C2 factorial: which of direction/tree/fleet makes the 24 vs 26 reversal';
    blocks('github', 2, x.cand, `c2-cell${x.cell}`, hyp, [PUBLIC], x.cell === 0 ? {expect: '613.62'} : x.cell === 7 ? {expect: '617.16'} : {});
    blocks('github', 2, x.cand, `c2-cell${x.cell}`, hyp, SCREEN);
  }
  // Kaggle: Case 3 team records, transplanted trees and plan start rules on 6 directions x 21-23 vehicles.
  for (const {cand, label} of team(3)) blocks('kaggle', 3, cand, label, 'C3 team record re-evaluated with common evaluator', [PUBLIC, ...SCREEN]);
  const transplants = {base: L.parseDesign(L.caseInput(3).csv).nodes, minjae: L.parseDesign(fs.readFileSync(path.join(L.HERE, 'team/case3/minjae__ans_c3.csv'), 'utf8')).nodes};
  for (const {name, dir} of case3Dirs()) for (const fleet of [21, 22, 23]) {
    for (const [tn, nodes] of Object.entries(transplants)) {
      const cand = register(3, {dir, fleet, nodes}, `c3-${name}-n${fleet}-transplant-${tn}`, 'transplant control');
      blocks('kaggle', 3, cand, `c3-${name}-n${fleet}-transplant-${tn}`, 'C3 control: existing tree moved to another direction/fleet', [PUBLIC, ...SCREEN]);
    }
    for (const [sn, nodes] of Object.entries(startTrees(3, dir))) {
      const cand = register(3, {dir, fleet, nodes}, `c3-${name}-n${fleet}-start-${sn}`, 'plan start rule');
      blocks('kaggle', 3, cand, `c3-${name}-n${fleet}-start-${sn}`, 'C3 start rule before search', [PUBLIC, ...SCREEN]);
    }
  }
  fs.writeFileSync(path.join(L.HERE, 'jobs', 'case3-dirs.json'), JSON.stringify(case3Dirs(), null, 1));
} else {
  throw Error('Unknown batch ' + batch + ' (later batches are built by plan2.cjs from collected results)');
}

fs.mkdirSync(path.join(L.HERE, 'jobs'), {recursive: true});
fs.writeFileSync(path.join(L.HERE, 'jobs', batch + '.jsonl'), tasks.map(t => JSON.stringify(t)).join('\n') + '\n');
const by = {};
for (const t of tasks) by[t.env] = (by[t.env] || 0) + 1;
console.log(batch, 'tasks', tasks.length, JSON.stringify(by), 'reused', reused.length);
module.exports = {};
