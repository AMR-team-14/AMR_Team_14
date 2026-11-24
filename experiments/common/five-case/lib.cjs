// Shared evaluator library for experiments/caseN result logs. Node.js standard library only.
// Every environment (local, GitHub Actions, Kaggle) loads the same inputs.json and jsim.cjs.
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const HERE = __dirname;
const lf = text => text.replace(/\r\n/g, '\n');
const sha = text => crypto.createHash('sha256').update(text).digest('hex');
const read = name => lf(fs.readFileSync(path.join(HERE, name), 'utf8'));

const INPUTS = JSON.parse(read('inputs.json'));
const JSIM = read('jsim.cjs');
const AGG = 'pooled-5seed-p95floor';       // web agvEvaluate_: pool waits of the block, mean + floor(0.95(N-1)) urgent quantile
const ENGINE = 'jsim:' + sha(JSIM).slice(0, 12) + '+ev1';
const FEATS = ['urg', 'w', 'de', 'dl', 'qo'];

function caseInput(c) {
  const item = INPUTS[c - 1];
  if (!item) throw Error('No input for case ' + c);
  return item;
}
const inputHash = c => sha(JSON.stringify(caseInput(c).p));

function network(p, bits) {
  const n = p.nodes.length;
  const D = Array.from({length: n}, (_, i) => Array.from({length: n}, (_, j) => i === j ? 0 : Infinity));
  const NX = Array.from({length: n}, (_, i) => Array.from({length: n}, (_, j) => i === j ? i : -1));
  p.edges.forEach(([u, v, d], k) => {
    if (bits[k]) [u, v] = [v, u];
    D[u][v] = d;
    NX[u][v] = v;
  });
  for (let k = 0; k < n; k++) for (let i = 0; i < n; i++) for (let j = 0; j < n; j++) {
    const d = D[i][k] + D[k][j];
    if (d < D[i][j]) { D[i][j] = d; NX[i][j] = NX[i][k]; }
  }
  if (D.some(row => row.includes(Infinity))) throw Error('Direction is not strongly connected');
  return {D, NX};
}

const engines = new Map();
function engine(c, dir) {
  const k = c + '|' + dir;
  let e = engines.get(k);
  if (e) return e;
  const p = caseInput(c).p;
  const mod = {exports: {}};
  const req = name => {
    if (name === './web/engine_raw.js') return {DATA_AGV: {...p, fixedDir: dir}, agvNet_: network};
    if (name === 'worker_threads') return {isMainThread: true};
    throw Error('Unexpected dependency ' + name);
  };
  Function('require', 'module', JSIM)(req, mod);
  e = mod.exports;
  if (engines.size > 64) engines.clear();
  engines.set(k, e);
  return e;
}

// Design text: "dir,<bits>\nfleet,<n>\nkey,id,f,min|max,th,lo,hi..." (submission CSV).
function parseDesign(text) {
  const rows = lf(text).trim().split('\n').map(r => r.trim()).filter(Boolean);
  if (!/^dir,[01]+$/.test(rows[0] || '')) throw Error('First row must be dir');
  if (!/^fleet,\d+$/.test(rows[1] || '')) throw Error('Second row must be fleet');
  const nodes = rows.slice(2).map(r => {
    const x = r.split(',');
    if (x[0] !== 'key' || x.length !== 7) throw Error('Bad key row ' + r);
    return {id: +x[1], f: x[2], mx: x[3], th: +x[4], lo: +x[5], hi: +x[6]};
  });
  return {dir: rows[0].slice(4), fleet: +rows[1].slice(6), nodes};
}
const fmtNum = v => String(+(+v).toFixed(6));
const treeText = nodes => nodes.map(n => [n.id, n.f, n.mx, fmtNum(n.th), n.lo, n.hi].join(',')).join(';');
function designText(d) {
  return ['dir,' + d.dir, 'fleet,' + d.fleet, ...d.nodes.map(n => 'key,' + [n.id, n.f, n.mx, fmtNum(n.th), n.lo, n.hi].join(','))].join('\n') + '\n';
}

// Renumber nodes in breadth-first order from the root so equal trees get equal text.
function canonical(d) {
  const byId = new Map(d.nodes.map(n => [n.id, n]));
  const order = [], map = new Map();
  const queue = [1];
  while (queue.length) {
    const id = queue.shift();
    const n = byId.get(id);
    if (!n || map.has(id)) continue;
    map.set(id, order.length + 1);
    order.push(n);
    for (const ch of [n.lo, n.hi]) if (ch) queue.push(ch);
  }
  const nodes = order.map(n => ({...n, id: map.get(n.id), lo: n.lo ? map.get(n.lo) : 0, hi: n.hi ? map.get(n.hi) : 0}));
  return {dir: d.dir, fleet: d.fleet, nodes};
}

// Same structural rules the web parser enforces (see plan: <=9 nodes, root 1, a feature once per path, urg at 0.5).
function validate(c, d) {
  const p = caseInput(c).p;
  if (d.dir.length !== p.edges.length) throw Error('Direction length ' + d.dir.length + ' != ' + p.edges.length);
  if (!(d.fleet >= p.nMin && d.fleet <= p.nMax)) throw Error('Fleet out of range');
  if (!d.nodes.length || d.nodes.length > p.maxNodes) throw Error('Node count');
  const byId = new Map();
  for (const n of d.nodes) {
    if (!Number.isInteger(n.id) || n.id < 1 || byId.has(n.id)) throw Error('Bad node id ' + n.id);
    if (!FEATS.includes(n.f) || !['min', 'max'].includes(n.mx) || !Number.isFinite(n.th)) throw Error('Bad node ' + n.id);
    if (n.f === 'urg' && n.th !== 0.5) throw Error('urg threshold must be 0.5');
    byId.set(n.id, n);
  }
  if (!byId.has(1)) throw Error('Missing root');
  const seen = new Set();
  const walk = (id, used) => {
    if (seen.has(id)) throw Error('Node reused ' + id);
    seen.add(id);
    const n = byId.get(id);
    if (!n) throw Error('Missing child ' + id);
    if (used.has(n.f)) throw Error('Feature repeated on a path at ' + id);
    const u = new Set(used).add(n.f);
    for (const ch of [n.lo, n.hi]) if (ch) walk(ch, u);
  };
  walk(1, new Set());
  if (seen.size !== byId.size) throw Error('Unreachable nodes');
  network(p, d.dir.split('').map(Number));
  return true;
}

function candidate(c, d) {
  const cd = canonical(d);
  validate(c, cd);
  const text = designText(cd);
  return {design: cd, text, sha: sha(text)};
}

// Evaluate one block of `count` consecutive seeds with the web aggregation. Rejected seeds are all listed.
function evaluate(c, d, start, count) {
  const p = caseInput(c).p;
  const sim = engine(c, d.dir);
  const ds = sim.parseSpec(d.fleet, treeText(d.nodes));
  const waits = [], urgent = [], rejected = [];
  for (let s = start; s < start + count; s++) {
    if (sim.simulate(ds, s, 0, waits, urgent).unstable) rejected.push(s);
  }
  if (rejected.length) return {score: null, rejected};
  urgent.sort((a, b) => a - b);
  const rate = p.alpha * p.flows.reduce((t, f) => t + f[2], 0);
  const mean = waits.reduce((t, w) => t + w, 0) / waits.length;
  const u95 = urgent.length ? urgent[Math.floor(0.95 * (urgent.length - 1))] : 0;
  const veh = p.cAGV * d.fleet, wc = p.cW * rate * mean, uc = p.cU * u95;
  const score = veh + wc + uc;
  if (!Number.isFinite(score)) throw Error('Invalid evaluation');
  return {score, veh, wc, uc, mean, u95, rejected, nWait: waits.length, nUrgent: urgent.length};
}

// Station-to-station distances in a direction, used to pick thresholds that actually split candidates.
function geometry(c, dir) {
  const p = caseInput(c).p;
  const {D} = network(p, dir.split('').map(Number));
  const st = p.pd;
  const de = new Set([0]);
  for (const a of st) for (const b of st) de.add(D[a][b]);
  const dl = p.flows.map(([o, dd, f]) => ({d: D[st[o]][st[dd]], f}));
  const sumFD = dl.reduce((t, x) => t + x.d * x.f, 0);
  return {de: [...de].sort((a, b) => a - b), dl: [...new Set(dl.map(x => x.d))].sort((a, b) => a - b), flows: dl, sumFD};
}

// Directions whose every maximal degree-2 chain is oriented consistently, filtered to strongly connected ones.
function directions(c) {
  const p = caseInput(c).p;
  const deg = new Array(p.nodes.length).fill(0);
  p.edges.forEach(([u, v]) => { deg[u]++; deg[v]++; });
  const chainOf = new Array(p.edges.length).fill(-1);
  const chains = [];
  for (let e = 0; e < p.edges.length; e++) {
    if (chainOf[e] >= 0) continue;
    const ch = [];
    const stack = [e];
    chainOf[e] = chains.length;
    while (stack.length) {
      const k = stack.pop();
      ch.push(k);
      for (const node of [p.edges[k][0], p.edges[k][1]]) {
        if (deg[node] !== 2) continue;
        for (let e2 = 0; e2 < p.edges.length; e2++) {
          if (chainOf[e2] < 0 && (p.edges[e2][0] === node || p.edges[e2][1] === node)) { chainOf[e2] = chains.length; stack.push(e2); }
        }
      }
    }
    chains.push(ch);
  }
  // Orientation inside a chain: walk it and record whether each edge follows the walk direction.
  const flipsFor = ch => {
    const set = new Set(ch);
    const ends = [];
    for (const k of ch) for (const node of p.edges[k].slice(0, 2)) {
      const inChain = ch.filter(e2 => p.edges[e2][0] === node || p.edges[e2][1] === node).length;
      if (deg[node] !== 2 || inChain === 1) ends.push(node);
    }
    let node = ends.length ? ends[0] : p.edges[ch[0]][0];
    const fwd = new Map();
    const left = new Set(set);
    while (left.size) {
      const k = [...left].find(e2 => p.edges[e2][0] === node || p.edges[e2][1] === node);
      if (k === undefined) break;
      left.delete(k);
      fwd.set(k, p.edges[k][0] === node ? 0 : 1);
      node = p.edges[k][0] === node ? p.edges[k][1] : p.edges[k][0];
    }
    return fwd;
  };
  const orient = chains.map(flipsFor);
  const out = [];
  for (let mask = 0; mask < (1 << chains.length); mask++) {
    const bits = new Array(p.edges.length).fill(0);
    chains.forEach((ch, i) => {
      const reverse = (mask >> i) & 1;
      for (const k of ch) bits[k] = orient[i].get(k) ^ reverse;
    });
    try { network(p, bits); out.push(bits.join('')); } catch { /* not strongly connected */ }
  }
  return {chains: chains.length, dirs: out};
}

module.exports = {HERE, INPUTS, AGG, ENGINE, FEATS, sha, lf, caseInput, inputHash, network, engine, parseDesign, designText,
  treeText, canonical, validate, candidate, evaluate, geometry, directions};
