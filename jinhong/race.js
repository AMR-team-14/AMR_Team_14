// 단계 선별(racing) 탐색. 승민의 A~D 단계 · 민재의 조기 컷 · 윤빈의 짝비교를 한 루프로 묶었다.
//  - 후보는 현재 최고안(incumbent)을 1~4곳 바꿔 만든다.
//  - 같은 시드 묶음에서 최고안과 짝비교하며 4 → 16 → 48 → 120묶음(600시드)으로 늘린다. 단계마다 가망 없는 후보를 버린다.
//  - 120묶음에서 t < −2이고 0.4점 넘게 낮으면 최고안을 바꾼다. 노드가 줄면서 비기는 안도 받는다(자리 확보).
//  - 여기서 쓴 시드(100001~)는 학습용이다. 최종 판정은 final2.js가 다른 시드로 한다.
// 사용: node race.js <시작 csv> <분> <출력 접두어> [난수 씨앗] [옵션: fixroot=1 nmin=18 nmax=23]
const fs = require('fs');
const { Engine, readSpec, specToCsv, paired, mean } = require('./cx.js');

const GRID = {
  urg: [0.5],
  w: [0.001, 0.02, 0.05, 0.1, 0.2, 0.3, 0.5, 0.75, 1, 1.25, 1.5, 2, 2.25, 2.45, 2.75, 3, 3.5, 4, 5, 6.5, 8],
  de: [0, 25, 50, 65, 90, 115, 140, 155, 180],
  dl: [25, 50, 65, 90],
  qo: [0, 1, 2, 3, 4, 6],
};
const FEATS = Object.keys(GRID);

// ── 나무 표현: {f, d, th, lo, hi} 중첩 객체(lo · hi가 null이면 잎) ──
function toTree(spec) {
  const m = {};
  for (const s of spec.nodes.split(';')) { const [id, f, d, th, lo, hi] = s.split(','); m[id] = { f, d, th: +th, lo: +lo, hi: +hi }; }
  const build = id => id && m[id] ? { f: m[id].f, d: m[id].d, th: m[id].th, lo: build(m[id].lo), hi: build(m[id].hi) } : null;
  return { n: spec.n, root: build(1) };
}
function toSpec(t) {
  const out = []; let next = 1; const q = [[t.root, next++]];
  while (q.length) {
    const [nd, id] = q.shift();
    const lo = nd.lo ? next++ : 0, hi = nd.hi ? next++ : 0;
    out.push(`${id},${nd.f},${nd.d},${nd.th},${lo},${hi}`);
    if (nd.lo) q.push([nd.lo, lo]); if (nd.hi) q.push([nd.hi, hi]);
  }
  return { n: t.n, nodes: out.join(';') };
}
const clone = t => JSON.parse(JSON.stringify(t));
function walk(nd, anc, fn, parent, side) { if (!nd) return; fn(nd, anc, parent, side); walk(nd.lo, anc.concat(nd.f), fn, nd, 'lo'); walk(nd.hi, anc.concat(nd.f), fn, nd, 'hi'); }
function count(nd) { return nd ? 1 + count(nd.lo) + count(nd.hi) : 0; }
function feats(nd, s = new Set()) { if (nd) { s.add(nd.f); feats(nd.lo, s); feats(nd.hi, s); } return s; }
function valid(t) {
  if (!t.root || count(t.root) > 9) return false;
  let ok = true; walk(t.root, [], (nd, anc) => { if (anc.includes(nd.f)) ok = false; });
  return ok;
}
function show(nd) { if (!nd) return '-'; const s = `${nd.f}(${nd.d},${nd.th})`; return nd.lo || nd.hi ? `${s}[${show(nd.lo)},${show(nd.hi)}]` : s; }

function makeRng(seed) { let s = seed | 0; return () => { s = (s + 0x6D2B79F5) | 0; let t = Math.imul(s ^ (s >>> 15), 1 | s); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }; }

function mutator(rnd, opt) {
  const pick = a => a[Math.floor(rnd() * a.length)];
  const randNode = anc => { const fs2 = FEATS.filter(f => !anc.includes(f)); if (!fs2.length) return null; const f = pick(fs2); return { f, d: pick(['min', 'max']), th: pick(GRID[f]), lo: null, hi: null }; };
  function one(t) {
    const nodes = []; walk(t.root, [], (nd, anc, parent, side) => nodes.push({ nd, anc, parent, side }));
    const cand = opt.fixroot ? nodes.filter(x => x.parent) : nodes;
    if (!cand.length) return;
    const x = pick(cand), nd = x.nd, k = rnd();
    if (k < 0.28) { const g = GRID[nd.f], i = g.indexOf(nd.th); nd.th = i >= 0 && rnd() < 0.7 ? g[Math.max(0, Math.min(g.length - 1, i + (rnd() < 0.5 ? -1 : 1)))] : pick(g); }
    else if (k < 0.40) { const f = pick(FEATS); nd.f = f; nd.th = pick(GRID[f]); }
    else if (k < 0.48) nd.d = nd.d === 'min' ? 'max' : 'min';
    else if (k < 0.66) { // 빈 자리에 기준 더하기
      const slots = []; walk(t.root, [], (n2, anc) => { for (const s of ['lo', 'hi']) if (!n2[s]) slots.push([n2, s, anc.concat(n2.f)]); });
      if (slots.length) { const [p, s, anc] = pick(slots); p[s] = randNode(anc); }
    }
    else if (k < 0.78) { if (x.parent) x.parent[x.side] = null; } // 가지 자르기
    else if (k < 0.84) { const s = nd.lo; nd.lo = nd.hi; nd.hi = s; }
    else if (k < 0.89) { if (x.parent) x.parent[x.side] = (rnd() < 0.5 ? nd.lo : nd.hi) || nd.lo || nd.hi; } // 자식을 끌어올리기
    else if (k < 0.94) { const nn = randNode(x.anc); if (nn && x.parent) { nn[rnd() < 0.5 ? 'lo' : 'hi'] = nd; x.parent[x.side] = nn; } } // 위에 기준 끼워 넣기
    else if (k < 0.97) { const y = pick(nodes); if (y.parent && x.parent && y !== x) { const c = clone(y.nd); x.parent[x.side] = c; } } // 다른 가지 복사
    else t.n = Math.max(opt.nmin, Math.min(opt.nmax, t.n + (rnd() < 0.5 ? -1 : 1)));
  }
  return t0 => {
    for (let tries = 0; tries < 50; tries++) {
      const t = clone(t0), k = 1 + Math.min(3, Math.floor(-Math.log(1 - rnd()) / 1.0));
      if (opt.oldonly) { // 도착 가지(root.lo)는 실험 12에서 최적으로 확인 → 대기 가지(root.hi)와 대수만 바꾼다
        const sub = { n: t.n, root: t.root.hi || { f: 'de', d: 'min', th: 65, lo: null, hi: null } };
        for (let j = 0; j < k; j++) one(sub);
        t.root.hi = sub.root; t.n = sub.n;
      } else for (let j = 0; j < k; j++) one(t);
      if (valid(t)) return t;
    }
    return null;
  };
}

const TR0 = 100001;
const STAGES = [ // [누적 묶음 수, 통과 기준: 짝차이 평균이 이 값보다 작아야 함]
  [2, 6.0], [6, 2.5], [16, 0.5], [48, -0.2], [120, null],
];

async function main() {
  const [start, minutes, prefix, seedArg, ...rest] = process.argv.slice(2);
  const opt = { fixroot: 0, oldonly: 0, nmin: 17, nmax: 23, lambda: 280 };
  rest.forEach(a => { const [k, v] = a.split('='); opt[k] = +v; });
  const rnd = makeRng(+(seedArg || 1)), mut = mutator(rnd, opt), eng = new Engine();
  const logf = `${prefix}.log`, log = s => { console.log(s); fs.appendFileSync(logf, s + '\n'); };
  const job = (t, b0, b1, qcut) => ({ ...toSpec(t), seed0: TR0 + 5 * b0, count: 5 * (b1 - b0), block: 5, qcut });
  let inc = toTree(readSpec(start));
  let incCost = (await eng.run([job(inc, 0, 120, 0)]))[0];
  const seen = new Set([JSON.stringify(toSpec(inc))]), t0 = Date.now(), el = () => ((Date.now() - t0) / 60000).toFixed(1);
  log(`시작 ${start}: ${show(inc.root)} n${inc.n} | 학습 600시드 ${mean(incCost).toFixed(2)}`);
  let gen = 0, evals = 0, accepted = 0;
  while ((Date.now() - t0) / 60000 < +minutes) {
    gen++;
    let pool = [];
    for (let tries = 0; pool.length < opt.lambda && tries < opt.lambda * 30; tries++) {
      const t = mut(inc); if (!t) continue;
      const key = JSON.stringify(toSpec(t)); if (seen.has(key)) continue;
      seen.add(key); pool.push({ t, cost: [] });
    }
    if (!pool.length) { log('새 후보를 더 만들 수 없어 멈춤'); break; }
    let prev = 0; const made = pool.length; const pass = [];
    for (const [nb, thr] of STAGES) {
      const res = await eng.run(pool.map(c => job(c.t, prev, nb, 110)));
      evals += pool.length * (nb - prev) * 5;
      pool.forEach((c, i) => { c.cost = c.cost.concat(res[i] || [Infinity]); });
      pool = pool.filter(c => c.cost.every(Number.isFinite));
      if (prev === 0) { // 첫 단계 비용이 최고안이나 다른 후보와 완전히 같으면 행동이 같은 나무다 → 버린다
        const sig = new Set([incCost.slice(0, nb).map(x => x.toFixed(6)).join()]);
        pool = pool.filter(c => { const k2 = c.cost.map(x => x.toFixed(6)).join(); if (sig.has(k2)) return false; sig.add(k2); return true; });
      }
      pool.forEach(c => { c.p = paired(c.cost, incCost.slice(0, nb)); });
      pass.push(pool.length);
      if (thr !== null) pool = pool.filter(c => c.p.d < thr);
      prev = nb;
      if (!pool.length) break;
    }
    // 120묶음까지 온 후보는 기록해 둔다(나중에 공개 점수로 고를 때 씀)
    pool.forEach(c => fs.appendFileSync(`${prefix}_arch.jsonl`, JSON.stringify({ gen, d: +c.p.d.toFixed(3), se: +c.p.se.toFixed(3), t: +c.p.t.toFixed(2), spec: toSpec(c.t), show: show(c.t.root) }) + '\n'));
    const better = pool.filter(c => c.p.t < -2 && c.p.d < -0.4).sort((a, b) => a.p.d - b.p.d);
    const simpler = pool.filter(c => count(c.t.root) < count(inc.root) && c.t.n <= inc.n && c.p.d < 0.3).sort((a, b) => a.p.d - b.p.d);
    const win = better[0] || simpler[0];
    if (win) {
      accepted++; inc = win.t; incCost = win.cost;
      fs.writeFileSync(`${prefix}_best.csv`, specToCsv(toSpec(inc)));
      log(`세대 ${gen} (${el()}분): ${better[0] ? '개선' : '단순화'} ${win.p.d.toFixed(2)} ± ${win.p.se.toFixed(2)} (t ${win.p.t.toFixed(1)}) → 학습 ${mean(incCost).toFixed(2)} | n${inc.n} ${show(inc.root)}`);
    } else if (gen % 5 === 0) log(`세대 ${gen} (${el()}분): 후보 ${made} → 단계 통과 ${pass.join(' → ')} | 채택 없음, 누적 평가 ${(evals / 1000).toFixed(0)}천 시드`);
  }
  log(`끝 (${el()}분, ${gen}세대, 채택 ${accepted}회, 평가 ${(evals / 1000).toFixed(0)}천 시드): 학습 ${mean(incCost).toFixed(2)} | n${inc.n} ${show(inc.root)}`);
  eng.close();
}
if (require.main === module) main();
module.exports = { toTree, toSpec, show, count, valid, GRID, FEATS };
