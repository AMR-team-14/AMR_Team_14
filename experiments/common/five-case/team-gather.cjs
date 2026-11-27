// Gather every answer CSV the team has pushed and score them all the same way, so all work is one experiment pool.
//   node team-gather.cjs [--cases 1,2,3,4,5] [--top 10] [--max500 1000] [--workers <cpus>]
// 1. Fetches every remote branch (depth 1, no blobs) except jsm, and lists answer-like CSVs for the cases
//    (paths with caseN / cN, files named answer*, ans*, cand*, *best*; legacy/ and bulk folders are skipped).
//    jsm's own candidates (experiments/caseN/candidates/*.csv) join the same pool from the working tree.
// 2. Scores each distinct answer (by sha256 of its text) with simulator/score.cjs on the public seeds 11-15.
// 3. Scores pooled seeds 1-500 (the team's common check) for every answer file. Best trees found by jsm searches
//    (raw/**/*.jsonl) also join the pool; of those only the top N per case by public cost get 1-500.
//    Case 1 is graded on a hidden seed and is ranked by the 1-500 cost. Scoring runs on all CPU cores.
// Each answer keeps who made it (owner: the branch's main committer, or the name prefix in "name__file.csv") and when
// this script first saw it, so credit stays with the person.
// Results are cached in team/scores.json, so each answer is scored once; sources keep branch, commit, path, author
// and any recorded public score (results.csv next to it, or the score in the file name) for a match check.
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {execFileSync} = require('node:child_process');
const L = require('./lib.cjs');

const HERE = __dirname;
const REPO = path.resolve(HERE, '..', '..', '..');
const arg = (k, d) => { const i = process.argv.indexOf(k); return i > 0 ? process.argv[i + 1] : d; };
const CASES = arg('--cases', '1,2,3,4,5').split(',').map(Number);
const TOP = +arg('--top', '10');
const MAX500 = +arg('--max500', '1000');
const WORKERS = +arg('--workers', String(require('node:os').cpus().length));
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
// Git names and file-name prefixes of the same person, shown as one name.
const PEOPLE = {'jante0': '박진홍', 'jinhong': '박진홍', 'pjh': '박진홍', 'kwak-kwak-2': '곽민재', 'minjae': '곽민재', 'kmj': '곽민재',
  'younbean': '윤빈', 'yb': '윤빈', 'Seungmin Jang': '장승민', 'jsm': '장승민'};
const who = n => PEOPLE[n] || n;
const HOUSEKEEPER = 'Seungmin Jang';          // owner of jsm; also made layout commits on every branch on 11/25
const SKIP = /(^|\/)(legacy|original|node_modules|docs|team-experiments|\.github)(\/|$)/;
const LIKE = /(answer|ans|cand|best)[^\/]*\.csv$/i;

function addSource(text, c, src) {
  const t = norm(text);
  if (!/^dir,[01]+\nfleet,\d+\n/.test(t)) return;
  // same design written differently (node order, number format) is one answer: key by the canonical design hash
  let h; try { h = L.candidate(c, L.parseDesign(t)).sha; } catch { h = sha(t); }
  const a = answers[h] || (answers[h] = {case: c, sha12: h.slice(0, 12), text: t, sources: [], firstSeen: new Date().toISOString()});
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
  try { git('fetch', '-q', '--depth=30', '--filter=blob:none', 'origin', `+refs/heads/${b}:refs/team/${b}`); }
  catch (e) { console.error(`skip ${b}: fetch failed`); continue; }
  const commit = git('rev-parse', `refs/team/${b}`).trim();
  // owner = most frequent committer in recent history, ignoring repository-wide housekeeping commits by the jsm owner
  const count = {};
  for (const n of git('log', '--format=%an', `refs/team/${b}`).trim().split('\n')) if (n && n !== HOUSEKEEPER) count[n] = (count[n] || 0) + 1;
  const author = Object.entries(count).sort((x, y) => y[1] - x[1])[0]?.[0] || (b.startsWith('jsm') ? HOUSEKEEPER : b);
  const files = git('ls-tree', '-r', '--name-only', `refs/team/${b}`).trim().split('\n');
  const blob = f => git('show', `refs/team/${b}:${f}`);
  for (const p of files) {
    if (SKIP.test(p) || !LIKE.test(p)) continue;
    let c = caseOf(p);
    if (c !== null && !CASES.includes(c)) continue;
    let text; try { text = blob(p); } catch { continue; }
    // the direction has a different bit count in every case (33, 36, 31, 76, 30): use it, the path can be silent or wrong
    const bits = (norm(text).match(/^dir,([01]+)\n/) || [])[1];
    const byBits = bits ? [1, 2, 3, 4, 5].find(k => L.caseInput(k).p.edges.length === bits.length) : null;
    if (byBits) c = byBits;
    if (!CASES.includes(c)) continue;
    const prefixed = path.posix.basename(p).match(/^([A-Za-z][\w-]*?)__/);
    addSource(text, c, {branch: b, commit: commit.slice(0, 10), path: p, author: who(prefixed ? prefixed[1] : author), recorded: recorded(b, files, p, blob)});
  }
}
const own = git('rev-parse', 'HEAD').trim().slice(0, 10);
for (const c of CASES) {
  const dir = path.join(REPO, 'experiments', 'case' + c, 'candidates');
  if (!fs.existsSync(dir)) continue;
  for (const f of fs.readdirSync(dir).filter(f => f.endsWith('.csv') && f !== 'index.csv'))
    addSource(fs.readFileSync(path.join(dir, f), 'utf8'), c, {branch: 'jsm', commit: own, path: `experiments/case${c}/candidates/${f}`, author: who(HOUSEKEEPER), recorded: null});
}

// jsm search results: every best tree a search returned
const walk = d => !fs.existsSync(d) ? [] : fs.readdirSync(d, {withFileTypes: true}).flatMap(e => e.isDirectory() ? walk(path.join(d, e.name)) : [path.join(d, e.name)]);
for (const f of walk(path.join(HERE, 'raw')).filter(f => f.endsWith('.jsonl'))) {
  const rel = path.relative(REPO, f).split(path.sep).join('/');
  for (const line of fs.readFileSync(f, 'utf8').split(/\r?\n/)) {
    if (!line.includes('"results"')) continue;
    let r; try { r = JSON.parse(line); } catch { continue; }
    const c = +((r.id || '').match(/:c(\d)/) || [])[1];
    if (!CASES.includes(c)) continue;
    for (const x of (r.out && r.out.results) || []) if (x.text)
      addSource(x.text, c, {branch: 'jsm', commit: own, path: `${rel}#${r.id}`, author: who(HOUSEKEEPER), recorded: null, kind: 'search'});
  }
}

// ---------- 2-3. score (parallel) ----------
const {execFile} = require('node:child_process');
function score(a, start, count) {
  const tmp = path.join(require('node:os').tmpdir(), `ans-${a.sha12}-${start}.csv`);
  fs.writeFileSync(tmp, a.text);
  return new Promise(res => execFile('node', [SCORE, String(a.case), tmp, String(start), String(count)], {cwd: path.dirname(SCORE), encoding: 'utf8'}, (err, out) => {
    if (err) return res({cost: null, rejected: null, error: String(err.message).split('\n')[0].slice(0, 120)});
    try { const r = JSON.parse(out); res({cost: r.pooled_cost, rejected: r.rejected.length}); } catch (e) { res({cost: null, rejected: null, error: 'bad output'}); }
  }));
}
async function pool(jobs) {
  let k = 0;
  await Promise.all(Array.from({length: Math.min(WORKERS, jobs.length)}, async () => { while (k < jobs.length) await jobs[k++](); }));
}
(async () => {
  const t0 = Date.now();
  const all = Object.values(answers);
  const p11 = all.filter(a => !a.public);
  await pool(p11.map(a => async () => { a.public = await score(a, 11, 5); }));
  const want = [];
  for (const c of CASES) {
    const ranked = all.filter(a => a.case === c && a.public && a.public.cost !== null).sort((x, y) => x.public.cost - y.public.cost);
    const top = new Set(ranked.slice(0, TOP));
    for (const a of ranked) if (!a.s500 && (top.has(a) || a.sources.some(s => s.kind !== 'search'))) want.push(a);
  }
  const p500 = want.slice(0, MAX500);
  await pool(p500.map(a => async () => { a.s500 = await score(a, 1, 500); }));
  for (const a of all) {
    a.fleet = +a.text.split('\n')[1].split(',')[1];
    for (const s of a.sources) s.match = s.recorded === null || !a.public || a.public.cost === null ? null : Math.abs(s.recorded - a.public.cost) < 0.006;
  }
  cache.updated = new Date().toISOString();
  cache.engine = 'simulator/score.cjs pooled';
  fs.mkdirSync(path.dirname(CACHE), {recursive: true});
  fs.writeFileSync(CACHE, JSON.stringify(cache, null, 1) + '\n');
  console.log(`branches ${branches.length}, answers ${all.length}, scored public ${p11.length}, scored 1-500 ${p500.length}, still without 1-500 ${want.length - p500.length}, workers ${WORKERS}, ${((Date.now() - t0) / 1000).toFixed(0)}s`);
})();
