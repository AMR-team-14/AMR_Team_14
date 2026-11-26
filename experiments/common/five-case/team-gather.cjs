// Gather every answer CSV the team has pushed and score them all the same way, so all work is one experiment pool.
//   node team-gather.cjs [--cases 3,4,5] [--top 10] [--max500 40]
// 1. Fetches every remote branch (depth 1, no blobs) except jsm, and lists answer-like CSVs for the cases
//    (paths with caseN / cN, files named answer*, ans*, cand*, *best*; legacy/ and bulk folders are skipped).
//    jsm's own candidates (experiments/caseN/candidates/*.csv) join the same pool from the working tree.
// 2. Scores each distinct answer (by sha256 of its text) with simulator/score.cjs on the public seeds 11-15.
// 3. Scores the top N per case by public cost on pooled seeds 1-500 (the team's common robustness check).
// Results are cached in team/scores.json, so each answer is scored once; sources keep branch, commit, path, author
// and any recorded public score (results.csv next to it, or the score in the file name) for a match check.
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {execFileSync} = require('node:child_process');

const HERE = __dirname;
const REPO = path.resolve(HERE, '..', '..', '..');
const arg = (k, d) => { const i = process.argv.indexOf(k); return i > 0 ? process.argv[i + 1] : d; };
const CASES = arg('--cases', '3,4,5').split(',').map(Number);
const TOP = +arg('--top', '10');
const MAX500 = +arg('--max500', '40');
const CACHE = path.join(HERE, 'team', 'scores.json');
const SCORE = path.join(REPO, 'simulator', 'score.cjs');
const git = (...a) => execFileSync('git', a, {cwd: REPO, encoding: 'utf8', maxBuffer: 1 << 28});
const sha = t => crypto.createHash('sha256').update(t).digest('hex');
const norm = t => t.replace(/\r\n/g, '\n').trim() + '\n';

const cache = fs.existsSync(CACHE) ? JSON.parse(fs.readFileSync(CACHE, 'utf8')) : {answers: {}};
const answers = cache.answers;

function caseOf(p) {
  const m = p.match(/case[_-]?(\d)/i) || p.match(/(?:^|[\/_])c(\d)(?:[\/_.-]|$)/i);
  return m ? +m[1] : null;
}
const SKIP = /(^|\/)(legacy|original|node_modules|docs|team-experiments|\.github)(\/|$)/;
const LIKE = /(answer|ans|cand|best)[^\/]*\.csv$/i;

function addSource(text, c, src) {
  const t = norm(text);
  if (!/^dir,[01]+\nfleet,\d+\n/.test(t)) return;
  const h = sha(t);
  const a = answers[h] || (answers[h] = {case: c, sha12: h.slice(0, 12), text: t, sources: []});
  const key = `${src.branch}:${src.path}`;
  const old = a.sources.find(s => `${s.branch}:${s.path}` === key);
  if (old) Object.assign(old, src); else a.sources.push(src);
}

// recorded public score: results.csv beside the answer, else a 5-digit score in the file name (79382 -> 793.82)
function recorded(branch, files, p, blob) {
  const dir = path.posix.dirname(p);
  const rc = files.find(f => f === dir + '/results.csv');
  if (rc) {
    try {
      const [head, ...rows] = blob(rc).trim().split(/\r?\n/).map(l => l.split(','));
      const i = k => head.findIndex(h => h.startsWith(k));
      const r = rows.find(r => r[i('first_seed')] === '11' && (r[i('seeds')] === '5' || r[i('seed_count')] === '5'));
      if (r && r[i('pooled_cost')]) return +r[i('pooled_cost')];
    } catch {}
  }
  const m = path.posix.basename(p).match(/(\d{5,6})(?=\D*\.csv$)/);
  if (m) { const v = +m[1] / 100; if (v > 300 && v < 2000) return v; }
  return null;
}

// ---------- 1. collect ----------
const branches = git('ls-remote', '--heads', 'origin').trim().split('\n').map(l => l.split('refs/heads/')[1]).filter(b => b && b !== 'jsm');
for (const b of branches) {
  try { git('fetch', '-q', '--depth=1', '--filter=blob:none', 'origin', `+refs/heads/${b}:refs/team/${b}`); }
  catch (e) { console.error(`skip ${b}: fetch failed`); continue; }
  const commit = git('rev-parse', `refs/team/${b}`).trim();
  const author = git('log', '-1', '--format=%an', `refs/team/${b}`).trim();
  const files = git('ls-tree', '-r', '--name-only', `refs/team/${b}`).trim().split('\n');
  const blob = f => git('show', `refs/team/${b}:${f}`);
  for (const p of files) {
    if (SKIP.test(p) || !LIKE.test(p)) continue;
    const c = caseOf(p);
    if (!CASES.includes(c)) continue;
    let text; try { text = blob(p); } catch { continue; }
    addSource(text, c, {branch: b, commit: commit.slice(0, 10), path: p, author, recorded: recorded(b, files, p, blob)});
  }
}
const own = git('rev-parse', 'HEAD').trim().slice(0, 10);
for (const c of CASES) {
  const dir = path.join(REPO, 'experiments', 'case' + c, 'candidates');
  if (!fs.existsSync(dir)) continue;
  for (const f of fs.readdirSync(dir).filter(f => f.endsWith('.csv') && f !== 'index.csv'))
    addSource(fs.readFileSync(path.join(dir, f), 'utf8'), c, {branch: 'jsm', commit: own, path: `experiments/case${c}/candidates/${f}`, author: 'jsm', recorded: null});
}

// ---------- 2-3. score ----------
function score(a, start, count) {
  const tmp = path.join(require('node:os').tmpdir(), `ans-${a.sha12}.csv`);
  fs.writeFileSync(tmp, a.text);
  try {
    const r = JSON.parse(execFileSync('node', [SCORE, String(a.case), tmp, String(start), String(count)], {cwd: path.dirname(SCORE), encoding: 'utf8'}));
    return {cost: r.pooled_cost, rejected: r.rejected.length};
  } catch (e) { return {cost: null, rejected: null, error: String(e.message).split('\n')[0].slice(0, 120)}; }
}
let n11 = 0, n500 = 0;
for (const a of Object.values(answers)) if (!a.public) { a.public = score(a, 11, 5); n11++; }
for (const c of CASES) {
  const ranked = Object.values(answers).filter(a => a.case === c && a.public && a.public.cost !== null).sort((x, y) => x.public.cost - y.public.cost);
  for (const a of ranked.slice(0, TOP)) {
    if (a.s500 || n500 >= MAX500) continue;
    a.s500 = score(a, 1, 500); n500++;
  }
}
for (const a of Object.values(answers)) {
  const fl = a.text.split('\n')[1].split(',')[1];
  a.fleet = +fl;
  for (const s of a.sources) s.match = s.recorded === null || !a.public || a.public.cost === null ? null : Math.abs(s.recorded - a.public.cost) < 0.006;
}
cache.updated = new Date().toISOString();
cache.engine = 'simulator/score.cjs pooled';
fs.mkdirSync(path.dirname(CACHE), {recursive: true});
fs.writeFileSync(CACHE, JSON.stringify(cache, null, 1) + '\n');
console.log(`branches ${branches.length}, answers ${Object.keys(answers).length}, scored public ${n11}, scored 1-500 ${n500}`);
