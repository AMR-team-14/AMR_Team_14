// Iterated local search over the whole answer: direction, fleet and tree together.
//   task: {type: 'ils', case, dir, fleet, start: <answer text>, budget, seed, objective: 'public'|'robust', kick: 3}
// Why: tree-only climbing (search.cjs) returns to the same floor. Larger neighbourhoods open new valleys:
//   - tree moves      (search.cjs neighbours: thresholds, min/max, feature, add/drop leaf, new root)
//   - fleet ±1
//   - corridor flip   (reverse one maximal degree-2 chain, kept only if strongly connected)
//   - cycle reversal  (reverse a whole directed cycle: always keeps strong connectivity)
// Local search = first improvement over a shuffled mix of these moves. At a local optimum the search "kicks":
// it reverses `kick` random cycles and perturbs one tree node, climbs again, and keeps the result if it is better.
// Case 1 has a fixed direction (direction moves off). Case 5 keeps edges 20 and 21 equal (bridge lanes).
'use strict';
const L = require('./lib.cjs');
const S = require('./search.cjs');

function chains(p) {
  const deg = new Array(p.nodes.length).fill(0);
  p.edges.forEach(([u, v]) => { deg[u]++; deg[v]++; });
  const of = new Array(p.edges.length).fill(-1), out = [];
  for (let e = 0; e < p.edges.length; e++) {
    if (of[e] >= 0) continue;
    const ch = [], st = [e]; of[e] = out.length;
    while (st.length) {
      const k = st.pop(); ch.push(k);
      for (const node of p.edges[k].slice(0, 2)) if (deg[node] === 2)
        p.edges.forEach((ed, e2) => { if (of[e2] < 0 && (ed[0] === node || ed[1] === node)) { of[e2] = out.length; st.push(e2); } });
    }
    out.push(ch);
  }
  return out;
}

// directed cycle through edge k (u->v under bits): shortest path v->u by BFS, plus k
function cycleThrough(p, bits, k) {
  const arcs = p.edges.map(([u, v], i) => bits[i] ? [v, u] : [u, v]);
  const [u, v] = arcs[k];
  const adj = new Map();
  arcs.forEach(([a, b], i) => { if (i !== k) { if (!adj.has(a)) adj.set(a, []); adj.get(a).push([b, i]); } });
  const prev = new Map([[v, null]]), q = [v];
  while (q.length) {
    const x = q.shift();
    if (x === u) break;
    for (const [y, i] of adj.get(x) || []) if (!prev.has(y)) { prev.set(y, [x, i]); q.push(y); }
  }
  if (!prev.has(u)) return null;
  const cyc = [k];
  for (let x = u; prev.get(x); x = prev.get(x)[0]) cyc.push(prev.get(x)[1]);
  return cyc;
}

function runIls(task) {
  const c = task.case, p = L.caseInput(c).p;
  const [S0, SN] = S.objectiveBlock(task.objective);
  const r = S.rng(task.seed || 1);
  const budget = task.budget || 400, kickSize = task.kick || 3;
  const fixedDir = c === 1;
  const lanes = c === 5 ? [20, 21] : null;
  const CH = chains(p);
  const cache = new Map();
  let evals = 0;
  const T = new Map();
  const thr = dir => { if (!T.has(dir)) T.set(dir, S.thresholdSets(c, dir)); return T.get(dir); };
  const okDir = bits => (!lanes || bits[lanes[0]] === bits[lanes[1]]);
  const score = d => {
    let cand;
    try { if (!okDir(d.dir.split('').map(Number))) return null; cand = L.candidate(c, d); } catch { return null; }
    if (cache.has(cand.sha)) return cache.get(cand.sha);
    if (evals >= budget) return undefined;
    evals++;
    const e = L.evaluate(c, cand.design, S0, SN);
    const v = {score: e.score === null ? Infinity : e.score, cand};
    cache.set(cand.sha, v);
    return v;
  };
  const dirMoves = d => {
    if (fixedDir) return [];
    const bits = d.dir.split('').map(Number), out = [];
    for (const ch of CH) out.push({kind: 'chain', apply: x => { const b = x.dir.split('').map(Number); for (const k of ch) b[k] ^= 1; x.dir = b.join(''); }});
    for (let t = 0; t < 12; t++) {
      const k = Math.floor(r() * p.edges.length);
      const cyc = cycleThrough(p, bits, k);
      if (cyc && cyc.length > 2) out.push({kind: 'cycle', apply: x => { const b = x.dir.split('').map(Number); for (const i of cyc) b[i] ^= 1; x.dir = b.join(''); }});
    }
    return out;
  };
  const fleetMoves = () => [-1, 1].map(df => ({kind: 'fleet', apply: x => { x.fleet += df; }}));
  const copy = d => ({dir: d.dir, fleet: d.fleet, nodes: S.clone(d.nodes)});

  const local = start => {
    let cur = start;
    for (;;) {
      const d = cur.cand.design;
      const moves = [...S.neighbours(d.nodes, p.maxNodes, thr(d.dir), r).map(m => ({kind: m.kind, apply: x => m.apply(x.nodes)})), ...fleetMoves(), ...dirMoves(d)];
      for (let i = moves.length - 1; i > 0; i--) { const j = Math.floor(r() * (i + 1)); [moves[i], moves[j]] = [moves[j], moves[i]]; }
      let better = null;
      for (const m of moves) {
        const x = copy(d); m.apply(x);
        const v = score(x);
        if (v === undefined) return {cur, done: true};
        if (v && v.score < cur.score - 0.01) { better = {v, kind: m.kind}; break; }
      }
      if (!better) return {cur, done: false};
      steps.push({kind: better.kind, score: +better.v.score.toFixed(4), evals});
      cur = better.v;
    }
  };

  const steps = [];
  const d0 = L.parseDesign(task.start);
  const start = score({dir: task.dir || d0.dir, fleet: task.fleet || d0.fleet, nodes: d0.nodes});
  if (!start) throw Error('Invalid start answer');
  let {cur, done} = local(start);
  let best = cur, kicks = 0;
  while (!done && evals < budget) {
    // kick: reverse a few cycles, nudge one tree node, then climb again
    const x = copy(best.cand.design);
    for (let q = 0; q < kickSize && !fixedDir; q++) {
      const ms = dirMoves(x).filter(m => m.kind === 'cycle');
      if (ms.length) ms[Math.floor(r() * ms.length)].apply(x);
    }
    const tm = S.neighbours(x.nodes, p.maxNodes, thr(x.dir), r);
    if (tm.length) tm[0].apply(x.nodes);
    if (r() < 0.3) x.fleet += r() < 0.5 ? -1 : 1;
    const s = score(x);
    kicks++;
    if (s === undefined) break;
    if (!s) continue;
    const res = local(s);
    done = res.done;
    if (res.cur.score < best.score - 0.01) { best = res.cur; steps.push({kind: 'kick', score: +best.score.toFixed(4), evals}); }
  }
  return {results: [{start: 'ils', score: best.score, sha: best.cand.sha, text: best.cand.text, evals, localOptimum: !done, kicks, steps: steps.slice(-30)}],
    evals, distinct: cache.size, objective: task.objective || 'public'};
}

module.exports = {runIls, cycleThrough, chains};
