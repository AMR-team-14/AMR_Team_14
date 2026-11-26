// Deterministic digest of every collected result, so the AI step only interprets numbers and never computes them.
//   node digest.cjs [--cases 3,4,5] [--alpha 0.05] [--out digest.json] [--save-state]
// Sources: jobs/*.jsonl (plan) and every *.jsonl under raw/ (results from local, GitHub and Kaggle; deduplicated by task id).
// Statistics (all on held-out seeds, paired on common seed blocks = common random numbers):
//   - paired t comparison of every candidate against the current best, Holm-adjusted across candidates;
//   - screening subset (Nelson et al. 2001 style, Bonferroni alpha/(k-1)): candidates not shown worse than any other;
//   - one-factor effects: candidate pairs that differ only in fleet, only in dir, or only in tree;
//   - OCBA allocation of the next confirm budget over the screening subset.
// Evidence ids (ev) are stable; reports/known-insights.md cites them as [ev:<id>=<verdict>] and this script flags
// any cited id whose verdict changed or disappeared. --save-state records the sources analyzed (reports/digest-state.json).
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const HERE = __dirname;
const REPO = path.resolve(HERE, '..', '..', '..');
const arg = (k, d) => { const i = process.argv.indexOf(k); return i > 0 ? process.argv[i + 1] : d; };
const CASES = arg('--cases', '1,2,3,4,5').split(',').map(Number);
const ALPHA = +arg('--alpha', '0.05');
const OUT = arg('--out', null);
const SAVE = process.argv.includes('--save-state');
const MIN_BLOCKS = 3;            // minimum common held-out blocks for any paired statement
const PUBLIC = 11;               // public block = seeds 11-15
// Grading: Case 1 is scored on a hidden seed (judge on held-out seeds); Cases 2-5 are scored on the public
// seeds 11-15 as they are (judge on the public score; held-out seeds only check robustness).
const OBJECTIVE = c => c === 1 ? 'heldout' : 'public';
const REPORTS = path.join(HERE, 'reports');
const STATE = path.join(REPORTS, 'digest-state.json');
const KNOWN = path.join(REPORTS, 'known-insights.md');

// ---------- statistics ----------
function lgamma(x) {
  const c = [76.18009172947146, -86.50532032941677, 24.01409824083091, -1.231739572450155, 0.1208650973866179e-2, -0.5395239384953e-5];
  let y = x, t = x + 5.5; t -= (x + 0.5) * Math.log(t);
  let s = 1.000000000190015; for (const v of c) s += v / ++y;
  return -t + Math.log(2.5066282746310005 * s / x);
}
function betacf(a, b, x) {
  let qab = a + b, qap = a + 1, qam = a - 1, c = 1, d = 1 - qab * x / qap;
  if (Math.abs(d) < 1e-30) d = 1e-30; d = 1 / d; let h = d;
  for (let m = 1; m <= 200; m++) {
    const m2 = 2 * m; let aa = m * (b - m) * x / ((qam + m2) * (a + m2));
    d = 1 + aa * d; if (Math.abs(d) < 1e-30) d = 1e-30; c = 1 + aa / c; if (Math.abs(c) < 1e-30) c = 1e-30; d = 1 / d; h *= d * c;
    aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2));
    d = 1 + aa * d; if (Math.abs(d) < 1e-30) d = 1e-30; c = 1 + aa / c; if (Math.abs(c) < 1e-30) c = 1e-30; d = 1 / d;
    const del = d * c; h *= del; if (Math.abs(del - 1) < 3e-12) break;
  }
  return h;
}
function ibeta(a, b, x) {
  if (x <= 0) return 0; if (x >= 1) return 1;
  const bt = Math.exp(lgamma(a + b) - lgamma(a) - lgamma(b) + a * Math.log(x) + b * Math.log(1 - x));
  return x < (a + 1) / (a + b + 2) ? bt * betacf(a, b, x) / a : 1 - bt * betacf(b, a, 1 - x) / b;
}
const tcdf = (t, df) => { const p = 0.5 * ibeta(df / 2, 0.5, df / (df + t * t)); return t > 0 ? 1 - p : p; };
function tq(p, df) {               // quantile of Student t by bisection
  let lo = -100, hi = 100;
  for (let i = 0; i < 100; i++) { const m = (lo + hi) / 2; if (tcdf(m, df) < p) lo = m; else hi = m; }
  return (lo + hi) / 2;
}
const mean = a => a.reduce((s, v) => s + v, 0) / a.length;
const sd = a => { if (a.length < 2) return NaN; const m = mean(a); return Math.sqrt(a.reduce((s, v) => s + (v - m) ** 2, 0) / (a.length - 1)); };
const pct = (a, q) => { const s = [...a].sort((x, y) => x - y); return s[Math.floor(q * (s.length - 1))]; };
const r2 = v => v === null || v === undefined || !Number.isFinite(v) ? null : Math.round(v * 100) / 100;
const r4 = v => v === null || v === undefined || !Number.isFinite(v) ? null : Math.round(v * 1e4) / 1e4;

// paired difference a - b on common blocks
function paired(a, b, level = 1 - ALPHA) {
  const common = [...a.blocks.keys()].filter(k => b.blocks.has(k));
  if (common.length < MIN_BLOCKS) return null;
  const d = common.map(k => a.blocks.get(k) - b.blocks.get(k));
  const n = d.length, m = mean(d), s = sd(d), se = s / Math.sqrt(n), df = n - 1;
  const t = se > 0 ? m / se : (m === 0 ? 0 : Math.sign(m) * Infinity);
  const half = se * tq(1 - (1 - level) / 2, df);
  const p = se > 0 ? 2 * (1 - tcdf(Math.abs(t), df)) : (m === 0 ? 1 : 0);
  return {n, diff: m, sd: s, se, lo: m - half, hi: m + half, p, aBetter: d.filter(v => v < 0).length};
}

// ---------- load plan and results ----------
const readJsonl = f => fs.readFileSync(f, 'utf8').split(/\r?\n/).filter(Boolean).map(l => JSON.parse(l));
const walk = d => !fs.existsSync(d) ? [] : fs.readdirSync(d, {withFileTypes: true}).flatMap(e =>
  e.isDirectory() ? walk(path.join(d, e.name)) : [path.join(d, e.name)]);
const rel = f => path.relative(REPO, f).split(path.sep).join('/');

const tasks = new Map();
for (const f of fs.readdirSync(path.join(HERE, 'jobs')).filter(n => n.endsWith('.jsonl')).sort())
  for (const t of readJsonl(path.join(HERE, 'jobs', f))) if (!tasks.has(t.id)) tasks.set(t.id, t);

const rawDirs = [path.join(HERE, 'raw'), path.join(REPO, 'experiments', 'JSM', 'five-case', 'raw')];
const files = rawDirs.flatMap(walk).filter(f => f.endsWith('.jsonl')).sort();
const engines = new Map();       // source file -> engine from sibling env.json
const results = new Map();       // id -> row
const conflicts = [], sources = [];
for (const f of files) {
  const envFile = f.replace(/\.jsonl$/, '.env.json');
  let engine = null;
  if (fs.existsSync(envFile)) { try { engine = JSON.parse(fs.readFileSync(envFile, 'utf8')).engine || null; } catch {} }
  const rows = readJsonl(f);
  const hash = crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex').slice(0, 12);
  sources.push({source: rel(f), rows: rows.length, sha12: hash, engine});
  for (const r of rows) {
    r._src = rel(f); r._engine = engine;
    const old = results.get(r.id);
    if (!old) { results.set(r.id, r); continue; }
    const same = JSON.stringify(old.out.score ?? old.out.results?.map(x => x.score)) === JSON.stringify(r.out.score ?? r.out.results?.map(x => x.score));
    if (!same) conflicts.push({id: r.id, a: old._src, b: r._src});
  }
}

// ---------- design helpers ----------
function parseDesignText(text) {
  const lines = String(text || '').split(/\r?\n/).filter(Boolean);
  const get = k => (lines.find(l => l.startsWith(k + ',')) || '').slice(k.length + 1);
  const tree = lines.filter(l => l.startsWith('key,')).join('\n');
  return {dir: get('dir'), fleet: +get('fleet') || null, tree12: crypto.createHash('sha256').update(tree).digest('hex').slice(0, 12)};
}
const dirIds = {};
const dirId = (c, dir) => {
  const m = dirIds[c] || (dirIds[c] = new Map());
  if (!m.has(dir)) m.set(dir, 'D' + (m.size + 1));
  return m.get(dir);
};

// ---------- coverage per batch ----------
const coverage = {};
for (const t of tasks.values()) {
  const b = coverage[t.batch] || (coverage[t.batch] = {batch: t.batch, cases: new Set(), byEnv: {}});
  b.cases.add(t.case);
  const e = b.byEnv[t.env] || (b.byEnv[t.env] = {planned: 0, returned: 0, failed: 0, rejectedBlocks: 0, cpuMs: 0, evals: 0});
  e.planned++;
  const r = results.get(t.id);
  if (!r) continue;
  e.returned++;
  e.cpuMs += r.ms || 0;
  if (r.out.status !== 'done') e.failed++;
  if (t.type === 'eval') { e.evals += 1; if ((r.out.rejected || []).length) e.rejectedBlocks++; }
  else e.evals += r.out.evals || 0;
}
const envHasAnyResult = (batch, env) => [...results.values()].some(r => r.batch === batch && r.env === env);
const coverageOut = Object.values(coverage).map(b => ({
  batch: b.batch,
  cases: [...b.cases].sort(),
  envs: Object.fromEntries(Object.entries(b.byEnv).map(([env, e]) => [env, {
    ...e, cpuMs: Math.round(e.cpuMs),
    missing: e.planned - e.returned,
    missingReason: e.planned === e.returned ? null : envHasAnyResult(b.batch, env) ? 'missing-in-returned-results' : 'not-collected',
    msPerEval: e.evals ? r2(e.cpuMs / e.evals) : null,
  }])),
}));

// ---------- per case ----------
const reported = (() => { try { return JSON.parse(fs.readFileSync(path.join(HERE, 'team', 'reported.json'), 'utf8')); } catch { return {}; } })();
const TEAM = (() => { try { return JSON.parse(fs.readFileSync(path.join(HERE, 'team', 'scores.json'), 'utf8')); } catch { return null; } })();
const evidence = {};             // id -> {verdict, ...}
const ev = (id, verdict, extra) => { evidence[id] = {verdict, ...extra}; return id; };

const casesOut = {};
for (const c of CASES) {
  const cands = new Map();       // candSha -> candidate
  const cand = (sha, text, label) => {
    let e = cands.get(sha);
    if (!e) {
      const d = parseDesignText(text);
      e = {sha12: sha.slice(0, 12), labels: new Set(), fleet: d.fleet, dir: d.dir ? dirId(c, d.dir) : null, dirFull: d.dir, tree12: d.tree12,
        pub: null, pubSrc: null, blocks: new Map(), blockRows: [], rejected: 0, batches: new Set(), engines: new Set()};
      cands.set(sha, e);
    }
    if (label) e.labels.add(label);
    return e;
  };
  const searches = [];
  for (const t of tasks.values()) {
    if (t.case !== c) continue;
    const r = results.get(t.id);
    if (!r || r.out.status !== 'done') continue;
    if (t.type === 'eval') {
      const e = cand(t.candSha, t.design, t.label);
      e.batches.add(t.batch); if (r._engine) e.engines.add(r._engine);
      if (r.out.score === null || r.out.score === undefined) { e.rejected++; continue; }
      if (t.start === PUBLIC && t.count === 5) { if (e.pub === null || r.out.score < e.pub) { e.pub = r.out.score; e.pubSrc = r._src; } }
      else if (t.count === 5) { e.blocks.set(t.start, r.out.score); e.blockRows.push(r.out); }
      else if (t.start === 1 && t.count === 500) e.s500 = r.out.score;
    } else if (t.type === 'search') {
      for (const x of r.out.results || []) {
        const e = cand(x.sha, x.text, `search:${t.label || t.id}:${x.start}`);
        e.batches.add(t.batch);
        if (e.pub === null || x.score < e.pub) { e.pub = x.score; e.pubSrc = r._src; }
        searches.push({batch: t.batch, task: t.id, start: x.start, fleet: e.fleet, dir: e.dir, sha12: e.sha12, score: x.score, evals: x.evals, localOptimum: x.localOptimum});
      }
    }
  }
  const all = [...cands.values()];
  const withPub = all.filter(e => e.pub !== null).sort((a, b) => a.pub - b.pub);
  const held = all.filter(e => e.blocks.size >= MIN_BLOCKS);

  // team record
  const team = Object.entries(reported).filter(([k]) => k.startsWith(c + ':')).map(([k, v]) => ({key: k, score: +v.score, same: v.same}))
    .filter(x => Number.isFinite(x.score)).sort((a, b) => a.score - b.score);
  const teamBest = team.find(x => x.same) || null;
  const bestPub = withPub[0] || null;
  const pubOut = bestPub ? {sha12: bestPub.sha12, labels: [...bestPub.labels].slice(0, 3), fleet: bestPub.fleet, dir: bestPub.dir, score: r4(bestPub.pub), source: bestPub.pubSrc} : null;
  const pubVsTeam = bestPub && teamBest ? r4(bestPub.pub - teamBest.score) : null;
  ev(`c${c}.public.best`, bestPub ? bestPub.sha12 : 'none', {score: pubOut && pubOut.score});

  // held-out summary per candidate
  const heldOut = held.map(e => {
    const s = [...e.blocks.values()];
    const vals = e.blockRows;
    return {sha12: e.sha12, labels: [...e.labels].slice(0, 3), fleet: e.fleet, dir: e.dir, tree12: e.tree12, pub: r4(e.pub), blocks: s.length,
      seedRange: `${Math.min(...e.blocks.keys())}-${Math.max(...e.blocks.keys()) + 4}`, mean: r2(mean(s)), p95: r2(pct(s, 0.95)), sd: r2(sd(s)),
      adjMean: null, pubMinusHeld: e.pub !== null ? r2(e.pub - mean(s)) : null,
      cost: {veh: r2(mean(vals.map(v => v.veh))), wait: r2(mean(vals.map(v => v.wc))), urgent: r2(mean(vals.map(v => v.uc)))}};
  }).sort((a, b) => a.mean - b.mean);

  // current best on held-out: lowest mean among candidates with the most-shared block family
  let best = null, vsBest = [], subset = [], ocba = null;
  if (held.length) {
    // CRN-adjusted ranking: reference R = most-evaluated candidate; adj(e) = mean(R) + paired(e - R) on common blocks.
    const R = held.reduce((x, y) => y.blocks.size > x.blocks.size || (y.blocks.size === x.blocks.size && mean([...y.blocks.values()]) < mean([...x.blocks.values()])) ? y : x);
    const mR = mean([...R.blocks.values()]);
    for (const e of held) { const p = e === R ? null : paired(e, R); e.adj = e === R ? mR : p ? mR + p.diff : mean([...e.blocks.values()]); e.adjBasis = e === R ? 'reference' : p ? 'paired' : 'raw'; }
    best = held.reduce((x, y) => y.adj < x.adj ? y : x);
    ev(`c${c}.held.best`, best.sha12, {adjMean: r2(best.adj)});
    // compare every candidate against best (Holm across comparisons)
    vsBest = held.filter(e => e !== best).map(e => ({e, p: paired(e, best)})).filter(x => x.p);
    const order = [...vsBest].sort((x, y) => x.p.p - y.p.p);
    let running = 0;
    order.forEach((x, i) => { running = Math.max(running, Math.min(1, (order.length - i) * x.p.p)); x.holm = running; });
    vsBest = vsBest.map(({e, p, holm}) => {
      const verdict = holm < ALPHA ? (p.diff > 0 ? 'worse' : 'better') : 'indistinct';
      const id = ev(`c${c}.vsbest.${e.sha12}`, verdict, {diff: r2(p.diff)});
      return {ev: id, sha12: e.sha12, labels: [...e.labels].slice(0, 2), fleet: e.fleet, dir: e.dir, n: p.n, diff: r2(p.diff),
        ci: [r2(p.lo), r2(p.hi)], p: r4(p.p), holmP: r4(holm), verdict, blocksBetter: p.aBetter};
    }).sort((a, b) => a.diff - b.diff);

    // screening subset: keep i unless some j is significantly better (one-sided, Bonferroni alpha/(k-1))
    const k = held.length;
    const lvl = 1 - 2 * ALPHA / Math.max(1, k - 1);     // two-sided level whose lower bound is one-sided alpha/(k-1)
    for (const i of held) {
      let out = null;
      for (const j of held) {
        if (i === j) continue;
        const p = paired(i, j, lvl);
        if (p && p.lo > 0) { out = j; break; }
      }
      ev(`c${c}.subset.${i.sha12}`, out ? 'eliminated' : 'in-subset');
      if (!out) subset.push(i);
    }

    // OCBA over the subset (needs >= 2), budget = 40 blocks per subset member
    if (OBJECTIVE(c) === 'heldout' && subset.length >= 2) {
      const b = subset.reduce((x, y) => y.adj < x.adj ? y : x);
      const others = subset.filter(e => e !== b).map(e => {
        const p = paired(e, b);
        const delta = p ? Math.max(Math.abs(p.diff), 0.05) : 1, sig = p && p.sd > 0 ? p.sd : sd([...e.blocks.values()]);
        return {e, w: (sig / delta) ** 2, sig};
      });
      const sb = sd([...b.blocks.values()]);
      const wb = sb * Math.sqrt(others.reduce((s, o) => s + (o.w ** 2) / (o.sig ** 2), 0));
      const total = 40 * subset.length, sum = wb + others.reduce((s, o) => s + o.w, 0);
      const usedMax = Math.max(...all.flatMap(e => [...e.blocks.keys()]));
      ocba = {budgetBlocks: total, nextStart: Math.ceil((usedMax + 5) / 10000) * 10000 + 1,
        alloc: [{sha12: b.sha12, role: 'current-best', blocks: Math.round(total * wb / sum)},
          ...others.map(o => ({sha12: o.e.sha12, role: 'challenger', blocks: Math.round(total * o.w / sum)}))]};
    }
  }

  // one-factor effects (pairs differing in exactly one of fleet / dir / tree)
  const effects = [];
  for (let i = 0; i < held.length; i++) for (let j = i + 1; j < held.length; j++) {
    const a = held[i], b = held[j];
    const diffs = [a.fleet !== b.fleet && 'fleet', a.dirFull !== b.dirFull && 'dir', a.tree12 !== b.tree12 && 'tree'].filter(Boolean);
    if (diffs.length !== 1) continue;
    const p = paired(a, b); if (!p) continue;
    const verdict = p.lo > 0 ? 'a-worse' : p.hi < 0 ? 'a-better' : 'indistinct';
    const id = ev(`c${c}.effect.${a.sha12}.${b.sha12}`, verdict, {diff: r2(p.diff)});
    effects.push({ev: id, factor: diffs[0], a: {sha12: a.sha12, fleet: a.fleet, dir: a.dir}, b: {sha12: b.sha12, fleet: b.fleet, dir: b.dir},
      n: p.n, diffAminusB: r2(p.diff), ci: [r2(p.lo), r2(p.hi)], verdict, aBetterBlocks: p.aBetter});
  }
  effects.sort((x, y) => Math.abs(y.diffAminusB / ((y.ci[1] - y.ci[0]) || 1)) - Math.abs(x.diffAminusB / ((x.ci[1] - x.ci[0]) || 1)));

  // search summary per batch
  const searchOut = {};
  for (const s of searches) {
    const o = searchOut[s.batch] || (searchOut[s.batch] = {runs: 0, evals: 0, localOptimum: 0, best: null, byFleet: {}});
    o.runs++; o.evals += s.evals || 0; if (s.localOptimum) o.localOptimum++;
    if (!o.best || s.score < o.best.score) o.best = {score: r4(s.score), sha12: s.sha12, fleet: s.fleet, dir: s.dir, start: s.start};
    const f = o.byFleet[s.fleet] || (o.byFleet[s.fleet] = {runs: 0, min: Infinity, scores: []});
    f.runs++; f.min = Math.min(f.min, s.score); f.scores.push(s.score);
  }
  for (const o of Object.values(searchOut)) for (const f of Object.values(o.byFleet)) { f.min = r2(f.min); f.mean = r2(mean(f.scores)); delete f.scores; }

  const bestSearchHeld = Object.values(searchOut).map(o => o.best).filter(Boolean).map(b => ({...b, heldOutBlocks: (all.find(e => e.sha12 === b.sha12) || {blocks: new Map()}).blocks.size}));

  const searchTasks = {};
  for (const t of tasks.values()) if (t.case === c && t.type === 'search') {
    const k = `${t.batch}:${t.env}`, o = searchTasks[k] || (searchTasks[k] = {planned: 0, returned: 0});
    o.planned++; if (results.has(t.id)) o.returned++;
  }
  // unified public ranking over every answer the team pushed (team/scores.json from team-gather.cjs)
  // Cases 2-5 rank by the graded public cost; Case 1 (hidden seed) ranks by pooled 1-500, unscored answers last
  const key = a => OBJECTIVE(c) === 'public' ? a.public.cost : (a.s500 && a.s500.cost !== null ? a.s500.cost : Infinity);
  const pool = TEAM ? Object.values(TEAM.answers).filter(a => a.case === c && a.public && a.public.cost !== null)
    .sort((x, y) => key(x) - key(y) || x.public.cost - y.public.cost || x.sha12.localeCompare(y.sha12)) : [];
  const pubRank = pool.slice(0, 10).map((a, i) => ({rank: i + 1, sha12: a.sha12, fleet: a.fleet,
    dir: dirId(c, a.text.split('\n')[0].slice(4)), score: r4(a.public.cost), gapToFirst: r4(key(a) - key(pool[0])),
    owners: [...new Set(a.sources.map(x => x.author))], firstSeen: a.firstSeen,
    s500: a.s500 ? a.s500.cost : null, s500Rejected: a.s500 ? a.s500.rejected : null,
    sources: a.sources.map(x => `${x.author} · ${x.branch}:${x.path}@${x.commit}`).slice(0, 3),
    recordedMismatch: a.sources.filter(x => x.match === false).map(x => `${x.branch}:${x.path} recorded ${x.recorded}`)}));
  if (pool.length) ev(`c${c}.rank.first`, pool[0].sha12, {score: r4(key(pool[0]))});
  casesOut[c] = {
    objective: OBJECTIVE(c) === 'public' ? 'public seeds 11-15 score (graded as is)' : 'hidden seed (judge on held-out seeds)',
    publicRanking: pubRank, teamPool: {answers: pool.length, updated: TEAM && TEAM.updated},
    candidates: all.length, searchTasks, withPublic: withPub.length, withHeldOut: held.length,
    engines: [...new Set(all.flatMap(e => [...e.engines]))],
    public: {best: pubOut, teamBest, diffVsTeam: pubVsTeam, top5: withPub.slice(0, 5).map(e => ({sha12: e.sha12, fleet: e.fleet, dir: e.dir, score: r4(e.pub)}))},
    heldOut: heldOut.map(h => ({...h, adjMean: r2((held.find(e => e.sha12 === h.sha12) || {}).adj)})).sort((a, b) => (a.adjMean ?? 1e9) - (b.adjMean ?? 1e9)).slice(0, 15), heldOutTotal: heldOut.length,
    currentBest: best ? {sha12: best.sha12, labels: [...best.labels].slice(0, 3), fleet: best.fleet, dir: best.dir, adjMean: r2(best.adj), rawMean: r2(mean([...best.blocks.values()])), blocks: best.blocks.size, basis: best.adjBasis} : null,
    vsBest: vsBest.slice(0, 15), vsBestTotal: vsBest.length,
    screeningSubset: subset.map(e => e.sha12),
    ocba,
    effects: effects.slice(0, 20), effectsTotal: effects.length,
    effectsSummary: effects.reduce((o, e) => { const f = o[e.factor] || (o[e.factor] = {'a-better': 0, 'a-worse': 0, indistinct: 0}); f[e.verdict]++; return o; }, {}),
    search: searchOut, bestSearchCandidates: bestSearchHeld,
    dirLegend: Object.fromEntries([...(dirIds[c] || new Map())].map(([d, id]) => [id, d])),
  };
}

// ---------- state: new sources, cited evidence ----------
const prev = (() => { try { return JSON.parse(fs.readFileSync(STATE, 'utf8')); } catch { return {sources: []}; } })();
const seen = new Set(prev.sources.map(s => s.source + '@' + s.sha12));
const newSources = sources.filter(s => !seen.has(s.source + '@' + s.sha12)).map(s => s.source);
const cited = [];
if (fs.existsSync(KNOWN)) {
  const re = /\[ev:([^\]=\s]+)=([^\]\s]+)\]/g; let m;
  const text = fs.readFileSync(KNOWN, 'utf8');
  while ((m = re.exec(text))) {
    const now = evidence[m[1]];
    cited.push({ev: m[1], recorded: m[2], current: now ? now.verdict : 'missing', status: !now ? 'recheck: evidence gone' : now.verdict === m[2] ? 'ok' : 'recheck: verdict changed'});
  }
}

const digest = {
  generated: new Date().toISOString(),
  method: {alpha: ALPHA, minBlocks: MIN_BLOCKS, publicSeeds: '11-15', heldOut: 'all other 5-seed blocks',
    paired: 'common random numbers: same seed block for both candidates', multiplicity: 'Holm across comparisons vs current best',
    screening: 'eliminate i if some j is better with one-sided Bonferroni alpha/(k-1)', ocba: 'Chen et al. OCBA, sigma from paired differences'},
  sources: {total: sources.length, new: newSources, conflicts},
  coverage: coverageOut,
  cases: casesOut,
  citedEvidence: cited,
  evidenceCount: Object.keys(evidence).length,
};
const text = JSON.stringify(digest, null, 1);
if (OUT) fs.writeFileSync(path.resolve(OUT), text); else process.stdout.write(text + '\n');
if (SAVE) {
  fs.mkdirSync(REPORTS, {recursive: true});
  fs.writeFileSync(STATE, JSON.stringify({saved: digest.generated, sources, evidence}, null, 1) + '\n');
}

// ---------- basic report (markdown, no AI) ----------
const MD = arg('--md', null);
if (MD) {
  const L = [];
  const tbl = (head, rows) => { L.push('| ' + head.join(' | ') + ' |', '|' + head.map(() => '---').join('|') + '|', ...rows.map(r => '| ' + r.map(v => v === null || v === undefined ? '–' : v).join(' | ') + ' |'), ''); };
  const newBatches = new Set([...results.values()].filter(r => newSources.includes(r._src)).map(r => r.batch));
  L.push('# 기본 분석 (코드 생성)', '', `- 생성: ${digest.generated}`, `- 새 결과 파일: ${newSources.length ? newSources.length + '개' : '없음'}`,
    `- 점수 충돌: ${conflicts.length ? conflicts.length + '건 ⚠' : '없음'}`, '- 점수는 비용이라 낮을수록 좋음. 별도 시드 비교는 같은 시드 블록끼리 대응 비교.', '');
  L.push('## 1. 회차·환경별 수행 현황' + (newBatches.size ? ' (새 결과가 있는 회차)' : ' (전체)'), '');
  const rows = [];
  for (const b of coverageOut) {
    if (newBatches.size && !newBatches.has(b.batch)) continue;
    for (const [env, e] of Object.entries(b.envs)) rows.push([b.batch, 'C' + b.cases.join(','), env, e.planned, e.returned,
      e.missing ? `${e.missing} (${e.missingReason === 'not-collected' ? '미회수' : '실제 누락'})` : 0, e.failed, e.rejectedBlocks, e.msPerEval, Math.round(e.cpuMs / 1000)]);
  }
  tbl(['회차', '케이스', '환경', '계획', '반환', '누락', '실패', '반려', '평가당 ms', 'CPU 초'], rows);
  for (const [c, x] of Object.entries(casesOut)) {
    L.push(`## Case ${c}`, '');
    const p = x.public;
    const f = x.publicRanking[0];
    L.push(`- 팀 전체 1위: **${f ? (OBJECTIVE(+c) === 'public' ? f.score : f.s500) : '데이터 없음'}**` + (f ? ` (${f.sha12}, ${f.owners.join(', ')}, ${f.fleet}대, ${f.dir})` : ''));
    L.push(`- 채점 기준: ${OBJECTIVE(+c) === 'public' ? '**공개 시드 11-15 점수 그대로** (별도 시드는 강건성 참고)' : '**비공개 시드** (별도 시드로 판정)'}`);
    if (x.currentBest) L.push(`- 별도 시드${OBJECTIVE(+c) === 'public' ? '(참고)' : ''} 현재 최고: **${x.currentBest.sha12}** (${x.currentBest.fleet}대, ${x.currentBest.dir}, 보정 평균 ${x.currentBest.adjMean}, ${x.currentBest.blocks}블록)`);
    L.push(`- 최고와 구별 안 되는 후보: ${x.screeningSubset.length}개` + (x.screeningSubset.length > 1 ? ` (${x.screeningSubset.slice(0, 8).join(', ')}${x.screeningSubset.length > 8 ? ' …' : ''})` : ''));
    const st = Object.entries(x.searchTasks).filter(([, v]) => v.planned !== v.returned).map(([k, v]) => `${k} ${v.returned}/${v.planned}`);
    if (st.length) L.push(`- 탐색 작업 미반환: ${st.join(', ')}`);
    L.push('');
    if (x.publicRanking.length) {
      const pub = OBJECTIVE(+c) === 'public';
      L.push(pub ? '**공개 시드 순위 — 팀 전체 답안 (채점 기준)**' : '**시드 1-500 순위 — 팀 전체 답안 (비공개 시드 채점이라 1-500 기준)**', '',
        '모든 브랜치의 답안을 공통 평가기(simulator/score.cjs)로 다시 채점. 만든 사람은 그 브랜치의 주 작성자(또는 파일명 앞 이름).', '');
      tbl(['순위', '후보', '만든 사람', '대수', '통로', '공개 11-15', '1-500', '1-500 반려', '1위와 차이', '출처'],
        x.publicRanking.map(r => [r.rank, r.sha12, r.owners.join(', '), r.fleet, r.dir, r.score, r.s500, r.s500Rejected, r.gapToFirst,
          r.sources.map(s => s.split(' · ')[1].split('@')[0]).join('<br>')]));
      const mm = x.publicRanking.flatMap(r => r.recordedMismatch);
      if (mm.length) L.push('기록 점수와 재채점이 다른 답안: ' + mm.join('; '), '');
    }
    if (x.heldOut.length) {
      L.push(OBJECTIVE(+c) === 'public' ? '**별도 시드 상위 후보 (강건성 참고)**' : '**별도 시드 상위 후보**', '');
      tbl(['후보', '대수', '통로', '블록', '평균', '95백분위', '보정 평균', '공개', '공개−별도', '차량/대기/긴급'],
        x.heldOut.slice(0, 8).map(h => [h.sha12, h.fleet, h.dir, h.blocks, h.mean, h.p95, h.adjMean, h.pub, h.pubMinusHeld, `${h.cost.veh}/${h.cost.wait}/${h.cost.urgent}`]));
    }
    if (x.vsBest.length) {
      L.push('**현재 최고 대비 대응 비교** (차이 > 0 이면 최고보다 나쁨, Holm 보정)', '');
      tbl(['후보', '대수', '통로', '블록', '차이', '95% CI', 'Holm p', '판정'],
        x.vsBest.slice(0, 8).map(v => [v.sha12, v.fleet, v.dir, v.n, v.diff, `[${v.ci[0]}, ${v.ci[1]}]`, v.holmP, {worse: '나쁨', better: '좋음', indistinct: '구별 안 됨'}[v.verdict]]));
    }
    const es = Object.entries(x.effectsSummary);
    if (es.length) {
      L.push('**한 요인만 다른 쌍** (a가 좋음 / a가 나쁨 / 구별 안 됨)', '');
      tbl(['요인', '쌍 수', 'a 좋음', 'a 나쁨', '구별 안 됨'], es.map(([f, v]) => [{fleet: '대수', dir: '통로', tree: '트리'}[f], v['a-better'] + v['a-worse'] + v.indistinct, v['a-better'], v['a-worse'], v.indistinct]));
    }
    const ss = Object.entries(x.search);
    if (ss.length) {
      L.push('**탐색(공개 시드 기준)**', '');
      tbl(['회차', '탐색 수', '평가 수', '국소최적', '최저', '최저 후보'], ss.map(([b, o]) => [b, o.runs, o.evals, o.localOptimum, o.best && o.best.score, o.best && `${o.best.sha12} (${o.best.fleet}대, ${o.best.dir})`]));
    }
    if (x.ocba) {
      L.push(`**다음 확인 단계 배분 (OCBA)**: 시드 ${x.ocba.nextStart}부터 총 ${x.ocba.budgetBlocks}블록 — ` + x.ocba.alloc.filter(a => a.blocks > 0).map(a => `${a.sha12} ${a.blocks}`).join(', '), '');
    }
  }
  fs.writeFileSync(path.resolve(MD), L.join('\n') + '\n');
}
