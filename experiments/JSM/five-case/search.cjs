// Deterministic single-thread local search over dispatch trees for a fixed case, direction and fleet.
// Objective: the public block (seeds 11-15, web aggregation). The final pick is re-checked on held-out seeds elsewhere.
'use strict';
const L = require('./lib.cjs');

function rng(seed) {
  let s = seed | 0;
  return () => { s = (s + 0x6D2B79F5) | 0; let t = Math.imul(s ^ (s >>> 15), 1 | s); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
}
const quantiles = (xs, k) => {
  const s = [...xs].sort((a, b) => a - b);
  return [...new Set(Array.from({length: k}, (_, i) => s[Math.floor((i + 0.5) / k * s.length)]))];
};
function weightedMedian(items) {
  const s = [...items].sort((a, b) => a.d - b.d);
  const tot = s.reduce((t, x) => t + x.f, 0);
  let acc = 0;
  for (const x of s) { acc += x.f; if (acc >= tot / 2) return x.d; }
  return s[s.length - 1].d;
}

function thresholdSets(c, dir) {
  const g = L.geometry(c, dir);
  return {
    sets: {urg: [0.5], w: [0, 0.001, 0.003, 0.01, 0.03, 0.1, 0.3, 0.5, 1, 2, 3.5, 5], de: g.de, dl: g.dl, qo: [0, 1, 2, 3, 4, 6]},
    dlMed: weightedMedian(g.flows),
    deMed: g.de[Math.floor(g.de.length / 2)],
  };
}

// Start rules from the plan: nearest car, far car for short normal loads, far car for long normal loads.
function startTrees(c, dir) {
  const {dlMed, deMed} = thresholdSets(c, dir);
  return {
    nearest: [{id: 1, f: 'urg', mx: 'max', th: 0.5, lo: 0, hi: 0}],
    shortFar: [{id: 1, f: 'urg', mx: 'max', th: 0.5, lo: 2, hi: 0}, {id: 2, f: 'dl', mx: 'min', th: dlMed, lo: 3, hi: 0}, {id: 3, f: 'de', mx: 'max', th: deMed, lo: 0, hi: 0}],
    longFar: [{id: 1, f: 'urg', mx: 'max', th: 0.5, lo: 2, hi: 0}, {id: 2, f: 'dl', mx: 'min', th: dlMed, lo: 0, hi: 3}, {id: 3, f: 'de', mx: 'max', th: deMed, lo: 0, hi: 0}],
  };
}

function pathFeatures(nodes, targetId) {
  const byId = new Map(nodes.map(n => [n.id, n]));
  const parent = new Map();
  for (const n of nodes) for (const ch of [n.lo, n.hi]) if (ch) parent.set(ch, n.id);
  const used = new Set();
  for (let id = targetId; id; id = parent.get(id)) used.add(byId.get(id).f);
  return used;
}
const clone = nodes => nodes.map(n => ({...n}));

function neighbours(nodes, maxNodes, T, r) {
  const out = [];
  const byId = new Map(nodes.map(n => [n.id, n]));
  const subFeatures = id => { const s = new Set(); const go = k => { const n = byId.get(k); s.add(n.f); if (n.lo) go(n.lo); if (n.hi) go(n.hi); }; go(id); return s; };
  for (const n of nodes) {
    for (const th of T.sets[n.f]) if (th !== n.th) out.push({kind: 'th', apply: ns => { ns.find(x => x.id === n.id).th = th; }});
    out.push({kind: 'flip', apply: ns => { const x = ns.find(y => y.id === n.id); x.mx = x.mx === 'min' ? 'max' : 'min'; }});
    // Change the feature of a node when no ancestor or descendant uses the new one.
    const blocked = new Set([...pathFeatures(nodes, n.id), ...subFeatures(n.id)]);
    for (const f of L.FEATS) {
      if (blocked.has(f)) continue;
      for (const th of quantiles(T.sets[f], 4)) for (const mx of ['min', 'max']) {
        out.push({kind: 'feat', apply: ns => { const x = ns.find(y => y.id === n.id); x.f = f; x.th = th; x.mx = mx; }});
      }
    }
    // Remove a leaf.
    if (!n.lo && !n.hi && n.id !== 1) out.push({kind: 'drop', apply: ns => {
      const i = ns.findIndex(y => y.id === n.id); ns.splice(i, 1);
      for (const y of ns) { if (y.lo === n.id) y.lo = 0; if (y.hi === n.id) y.hi = 0; }
    }});
    // Add a leaf under an empty slot.
    if (nodes.length < maxNodes) for (const side of ['lo', 'hi']) {
      if (n[side]) continue;
      const used = pathFeatures(nodes, n.id);
      for (const f of L.FEATS) {
        if (used.has(f)) continue;
        for (const th of quantiles(T.sets[f], 5)) for (const mx of ['min', 'max']) {
          out.push({kind: 'add', apply: ns => { const id = Math.max(...ns.map(y => y.id)) + 1; ns.find(y => y.id === n.id)[side] = id; ns.push({id, f, mx, th, lo: 0, hi: 0}); }});
        }
      }
    }
  }
  // Insert a new root above the current tree (needed to reach e.g. a w-root tree from an urg-root start).
  if (nodes.length < maxNodes) {
    const below = new Set(nodes.map(n => n.f));
    for (const f of L.FEATS) {
      if (below.has(f)) continue;
      for (const th of quantiles(T.sets[f], 4)) for (const mx of ['min', 'max']) for (const side of ['lo', 'hi']) {
        out.push({kind: 'root', apply: ns => {
          for (const y of ns) { y.id += 1; if (y.lo) y.lo += 1; if (y.hi) y.hi += 1; }
          ns.push({id: 1, f, mx, th, lo: side === 'lo' ? 2 : 0, hi: side === 'hi' ? 2 : 0});
        }});
      }
    }
  }
  for (let i = out.length - 1; i > 0; i--) { const j = Math.floor(r() * (i + 1)); [out[i], out[j]] = [out[j], out[i]]; }
  return out;
}

// First-improvement hill climbing with a shared cache; stops at a local optimum or when the budget is spent.
function climb(c, dir, fleet, startNodes, budget, seed, cache, counter) {
  const maxNodes = L.caseInput(c).p.maxNodes;
  const T = thresholdSets(c, dir);
  const r = rng(seed);
  const score = nodes => {
    let cand;
    try { cand = L.candidate(c, {dir, fleet, nodes}); } catch { return null; }
    if (cache.has(cand.sha)) return cache.get(cand.sha);
    if (counter.n >= counter.max) return undefined;
    counter.n++;
    const e = L.evaluate(c, cand.design, 11, 5);
    const v = {score: e.score === null ? Infinity : e.score, cand};
    cache.set(cand.sha, v);
    return v;
  };
  let cur = score(startNodes);
  if (!cur) throw Error('Invalid start tree');
  const steps = [];
  let used = 0;
  for (;;) {
    let improved = false;
    for (const mv of neighbours(cur.cand.design.nodes, maxNodes, T, r)) {
      if (used >= budget || counter.n >= counter.max) break;
      const ns = clone(cur.cand.design.nodes);
      mv.apply(ns);
      const before = counter.n;
      const v = score(ns);
      used += counter.n - before;
      if (v === undefined) break;
      if (v && v.score < cur.score - 0.01) {
        steps.push({kind: mv.kind, score: +v.score.toFixed(4), evals: used});
        cur = v; improved = true; break;
      }
    }
    if (!improved || used >= budget || counter.n >= counter.max) break;
  }
  return {best: cur, evals: used, steps, localOptimum: used < budget};
}

function runSearch(task) {
  const {case: c, dir, fleet, budget, seed} = task;
  let starts = startTrees(c, dir);
  if (task.transplant) starts.transplant = L.parseDesign(task.transplant).nodes;
  if (task.only) starts = {[task.only]: starts[task.only]};
  const cache = new Map();
  const counter = {n: 0, max: budget * Object.keys(starts).length};
  const results = [];
  let i = 0;
  for (const [name, nodes] of Object.entries(starts)) {
    const res = climb(c, dir, fleet, nodes, budget, seed + 7919 * i++, cache, counter);
    results.push({start: name, score: res.best.score, sha: res.best.cand.sha, text: res.best.cand.text, evals: res.evals, localOptimum: res.localOptimum, steps: res.steps});
  }
  return {results, evals: counter.n, distinct: cache.size};
}

module.exports = {runSearch, startTrees, thresholdSets};
