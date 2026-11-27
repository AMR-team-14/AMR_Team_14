// Turn the AI's next-experiment proposal (next.json) into a GitHub batch, with hard limits checked by code.
//   node next-plan.cjs <next.json> [--batch auto-MMDD-HHMM]
// next.json:
//   {"decision": "run" | "stop", "reason": "...",
//    "experiments": [
//      {"case": 5, "type": "search", "fleet": 24, "dir": "sha12:aed2f894f8a5" | "<0/1 bits>",
//       "start": "transplant:aed2f894f8a5" | "nearest" | "shortFar" | "longFar", "budget": 240, "repeats": 4,
//       "why": "..."},
//      {"case": 4, "type": "ils", "start": "sha12:55e571f4da46", "fleet": 18 (optional), "budget": 1500,
//       "repeats": 8, "objective": "public" | "robust", "kick": 3, "why": "..."},
//      {"case": 3, "type": "eval", "sha12": "2fcf90e69eb4", "fleet": 23 (optional override),
//       "seeds": "public" | "screen" | "confirm" | "final", "why": "..."}]}
// sha12 refers to any answer in team/scores.json or experiments/caseN/candidates/. Searches climb on the public seeds
// 11-15 (search.cjs); evals use the team seed plan (seeds.cjs). Writes jobs/<batch>-g0..7.jsonl and
// overnight-request.txt; prints a one-line summary. Exits 3 when there is nothing to run.
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const L = require('./lib.cjs');
const R = require('./results.cjs');
const S = require('./seeds.cjs');

const LIMITS = {experiments: 12, searches: 64, budget: 400, ilsBudget: 3000, repeats: 16, evalBlocks: 800, fleet: [1, 60]};
const OBJ = o => { const v = o || 'public'; if (!['public', 'robust'].includes(v)) fail('objective must be public or robust'); return v; };
const SHARDS = 8;
const file = process.argv[2];
const i = process.argv.indexOf('--batch');
const stamp = new Date(Date.now() + 9 * 3600e3).toISOString().slice(5, 16).replace(/[-T:]/g, '');
const batch = i > 0 ? process.argv[i + 1] : `auto-${stamp}`;
const fail = m => { console.error('next-plan: ' + m); process.exit(2); };

const spec = JSON.parse(fs.readFileSync(file, 'utf8'));
if (spec.decision !== 'run') { console.log(`next-plan: decision=${spec.decision || 'none'} (${spec.reason || ''})`); process.exit(3); }
const exps = spec.experiments || [];
if (!exps.length) { console.log('next-plan: no experiments'); process.exit(3); }
if (exps.length > LIMITS.experiments) fail(`too many experiments (${exps.length} > ${LIMITS.experiments})`);

// every known design by sha12
const known = new Map();
const team = (() => { try { return JSON.parse(fs.readFileSync(path.join(L.HERE, 'team', 'scores.json'), 'utf8')).answers; } catch { return {}; } })();
for (const a of Object.values(team)) known.set(a.sha12, {case: a.case, text: a.text});
for (const c of [1, 2, 3, 4, 5]) {
  const d = path.join(R.caseDir(c), 'candidates');
  if (fs.existsSync(d)) for (const f of fs.readdirSync(d).filter(f => /^[0-9a-f]{12}\.csv$/.test(f)))
    if (!known.has(f.slice(0, 12))) known.set(f.slice(0, 12), {case: c, text: fs.readFileSync(path.join(d, f), 'utf8')});
}
// designs that were only ever written into job files (evaluated, never saved as candidate files)
for (const f of fs.readdirSync(path.join(L.HERE, 'jobs')).filter(f => f.endsWith('.jsonl')))
  for (const line of fs.readFileSync(path.join(L.HERE, 'jobs', f), 'utf8').split(/\r?\n/)) {
    if (!line.includes('"candSha"')) continue;
    const t = JSON.parse(line);
    if (t.design && !known.has(t.candSha.slice(0, 12))) known.set(t.candSha.slice(0, 12), {case: t.case, text: t.design});
  }
const design = (c, ref) => {
  const k = String(ref).replace(/^sha12:/, '').slice(0, 12);
  const a = known.get(k);
  if (!a) fail(`unknown sha12 ${k}`);
  if (a.case !== c) fail(`sha12 ${k} belongs to case ${a.case}, not ${c}`);
  return L.parseDesign(a.text);
};
const edges = c => L.caseInput(c).p.edges.length;

const tasks = [];
let searches = 0, blocks = 0;
exps.forEach((x, n) => {
  const c = +x.case;
  if (![1, 2, 3, 4, 5].includes(c)) fail(`experiment ${n}: case must be 1-5`);
  const why = String(x.why || '').slice(0, 200);
  if (x.type === 'search') {
    const fleet = +x.fleet;
    if (!(fleet >= LIMITS.fleet[0] && fleet <= LIMITS.fleet[1])) fail(`experiment ${n}: fleet out of range`);
    const dir = /^[01]+$/.test(x.dir || '') ? x.dir : design(c, x.dir).dir;
    if (dir.length !== edges(c)) fail(`experiment ${n}: dir has ${dir.length} bits, case ${c} needs ${edges(c)}`);
    const start = String(x.start || 'transplant');
    let only = start, transplant;
    if (start.startsWith('transplant')) {
      const d = design(c, start.split(':')[1] || x.dir);
      transplant = L.candidate(c, {...d, dir, fleet}).text;   // validates the tree on this direction and fleet
      only = 'transplant';
    } else if (!['nearest', 'shortFar', 'longFar'].includes(start)) fail(`experiment ${n}: unknown start ${start}`);
    const budget = Math.min(+x.budget || 240, LIMITS.budget);
    const repeats = Math.min(Math.max(1, +x.repeats || 1), LIMITS.repeats);
    for (let r = 0; r < repeats; r++) {
      if (++searches > LIMITS.searches) fail(`more than ${LIMITS.searches} searches`);
      const seed = parseInt(crypto.createHash('sha256').update(`${batch}:${n}:${r}`).digest('hex').slice(0, 6), 16);
      tasks.push({id: `${batch}:c${c}:search:${n}:${r}`, batch, env: 'github', type: 'search', case: c, label: `${batch}:e${n}`,
        hypothesis: why, dir, fleet, only, transplant, budget, seed, objective: OBJ(x.objective)});
    }
  } else if (x.type === 'ils') {
    const d = design(c, x.start || x.sha12);
    const fleet = x.fleet ? +x.fleet : d.fleet;
    if (!(fleet >= LIMITS.fleet[0] && fleet <= LIMITS.fleet[1])) fail(`experiment ${n}: fleet out of range`);
    const start = L.candidate(c, {...d, fleet}).text;
    const budget = Math.min(+x.budget || 1500, LIMITS.ilsBudget);
    const repeats = Math.min(Math.max(1, +x.repeats || 1), LIMITS.repeats);
    for (let r = 0; r < repeats; r++) {
      if (++searches > LIMITS.searches) fail(`more than ${LIMITS.searches} searches`);
      const seed = parseInt(crypto.createHash('sha256').update(`${batch}:${n}:${r}`).digest('hex').slice(0, 6), 16);
      tasks.push({id: `${batch}:c${c}:ils:${n}:${r}`, batch, env: 'github', type: 'ils', case: c, label: `${batch}:e${n}`,
        hypothesis: why, start, budget, seed, objective: OBJ(x.objective), kick: Math.min(+x.kick || 3, 8)});
    }
  } else if (x.type === 'eval') {
    const d = design(c, x.sha12);
    const cand = L.candidate(c, x.fleet ? {...d, fleet: +x.fleet} : d);
    R.saveCandidate(c, cand, `${batch}:e${n}`, 'auto next-plan');
    const stage = x.seeds || 'screen';
    const starts = stage === 'public' ? [S.PUBLIC] : S.STAGES[stage];
    if (!starts) fail(`experiment ${n}: seeds must be public|screen|confirm|final`);
    const have = new Set(R.allResults(c).filter(r => r.status === 'done').map(R.keyOf));
    for (const s of starts) {
      const key = [c, cand.sha, L.inputHash(c), L.ENGINE, R.seedText(s, 5), L.AGG].join('|');
      if (have.has(key)) continue;
      if (++blocks > LIMITS.evalBlocks) fail(`more than ${LIMITS.evalBlocks} eval blocks`);
      tasks.push({id: `${batch}:c${c}:${cand.sha.slice(0, 12)}:${s}`, batch, env: 'github', type: 'eval', case: c, label: `${batch}:e${n}`,
        hypothesis: why, candSha: cand.sha, design: cand.text, start: s, count: 5});
    }
    if (stage === 'final') tasks.push({id: `${batch}:c${c}:${cand.sha.slice(0, 12)}:pooled1-500`, batch, env: 'github', type: 'eval', case: c,
      label: `${batch}:e${n}`, hypothesis: why, candSha: cand.sha, design: cand.text, start: S.POOLED.start, count: S.POOLED.count});
  } else fail(`experiment ${n}: type must be ils, search or eval`);
});
if (!tasks.length) { console.log('next-plan: everything requested is already done'); process.exit(3); }

// searches are long, evals short: deal searches first so every shard gets a similar load
tasks.sort((a, b) => (a.type === 'eval') - (b.type === 'eval'));
const shards = Array.from({length: SHARDS}, () => []);
tasks.forEach((t, k) => shards[k % SHARDS].push(t));
const jobs = path.join(L.HERE, 'jobs');
shards.forEach((s, k) => fs.writeFileSync(path.join(jobs, `${batch}-g${k}.jsonl`), s.map(t => JSON.stringify(t)).join('\n') + (s.length ? '\n' : '')));
fs.writeFileSync(path.join(jobs, `${batch}.plan.json`), JSON.stringify({batch, created: new Date().toISOString(), reason: spec.reason, experiments: exps}, null, 1) + '\n');
fs.writeFileSync(path.join(L.HERE, 'overnight-request.txt'), `${batch} 4\n`);
console.log(`next-plan: ${batch} — ${searches} searches, ${tasks.length - searches} evals over ${SHARDS} shards`);
