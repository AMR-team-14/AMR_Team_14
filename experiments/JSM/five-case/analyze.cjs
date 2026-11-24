// Paired comparison on held-out seed blocks.
//   node analyze.cjs <case> <baselineSha12> <stage: screen|confirm|final> [sha12 ...]   (no shas: every candidate with that stage complete)
'use strict';
const L = require('./lib.cjs');
const R = require('./results.cjs');
const [c, base12, stage, ...want] = process.argv.slice(2);
const N = {screen: 4, confirm: 40, final: 200}[stage];
const FIRST = {screen: 2400001, confirm: 2410001, final: 2420001}[stage];
const input = L.inputHash(+c);
const by = new Map();   // sha -> {blocks: Map(start -> score|null), pub, labels}
for (const r of R.allResults(c)) {
  if (r.status !== 'done' || r.input_sha256 !== input || r.evaluator !== L.ENGINE || r.aggregation !== L.AGG) continue;
  const e = by.get(r.candidate_sha256) || {blocks: new Map(), labels: new Set(), pub: null, fleet: r.fleet, dir: r.dir, tree: r.tree, path: r.candidate_path};
  e.labels.add(r.candidate);
  const start = +r.seeds.split('-')[0];
  const v = r.score === '' ? null : +r.score;
  if (r.seeds === '11-15') e.pub = v;
  else if (start >= FIRST && start < FIRST + 5 * N && (start - FIRST) % 5 === 0) e.blocks.set(start, v);
  by.set(r.candidate_sha256, e);
}
const base = [...by.entries()].find(([s]) => s.startsWith(base12));
if (!base) throw Error('baseline not found');
const tq = n => n >= 120 ? 1.97 : n >= 60 ? 2.0 : n >= 39 ? 2.023 : n >= 3 ? [12.706, 4.303, 3.182][Math.min(n - 1, 2)] || 2.1 : NaN;
const out = [];
for (const [sha, e] of by) {
  if (want.length && !want.some(w => sha.startsWith(w)) && sha !== base[0]) continue;
  if (e.blocks.size < N) continue;
  const scores = [...e.blocks.values()];
  const rej = scores.filter(v => v === null).length;
  const ok = scores.filter(v => v !== null).sort((a, b) => a - b);
  const d = [];
  for (const [s, v] of e.blocks) { const b = base[1].blocks.get(s); if (v !== null && b !== null && b !== undefined) d.push(v - b); }
  const mean = d.reduce((a, b) => a + b, 0) / d.length;
  const sd = Math.sqrt(d.reduce((a, b) => a + (b - mean) ** 2, 0) / (d.length - 1));
  const half = tq(d.length - 1) * sd / Math.sqrt(d.length);
  out.push({sha12: sha.slice(0, 12), labels: [...e.labels].join(' | ').slice(0, 70), fleet: e.fleet, pub: e.pub, mean: ok.reduce((a, b) => a + b, 0) / ok.length,
    p95: ok[Math.floor(0.95 * (ok.length - 1))], worst: ok[ok.length - 1], rejectedBlocks: rej, pairs: d.length,
    diff: sha === base[0] ? 0 : mean, lo: sha === base[0] ? 0 : mean - half, hi: sha === base[0] ? 0 : mean + half});
}
out.sort((a, b) => a.mean - b.mean);
const f = (v, k = 2) => v === null || v === undefined || Number.isNaN(v) ? '' : (+v).toFixed(k);
console.log(['sha12', 'fleet', 'public', `${stage}_mean`, 'p95', 'worst', 'rej', 'pairs', 'diff_vs_base', 'ci95_lo', 'ci95_hi', 'labels'].join('\t'));
for (const o of out) console.log([o.sha12, o.fleet, f(o.pub), f(o.mean), f(o.p95), f(o.worst), o.rejectedBlocks, o.pairs, f(o.diff), f(o.lo), f(o.hi), o.labels].join('\t'));
