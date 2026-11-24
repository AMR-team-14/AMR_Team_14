// 나무 모양까지 바꾸는 탐색 (1+λ 진화). 학습 시드로 고르고, 좋아진 안은 따로 남겨 둔 시드로 다시 본다.
// 사용: node evo.js <시작 csv> <세대 수> <출력 접두어> [난수 씨앗]
const { Worker, isMainThread, parentPort } = require('worker_threads');
const fs = require('fs');

if (!isMainThread) {
  const { evalDesign } = require('./fast.js');
  const { parse } = require('./eval.js');
  parentPort.on('message', ({ id, text, seeds }) => {
    let r; try { r = evalDesign(parse(text), seeds); } catch (e) { r = { stable: false, value: Infinity, bad: true }; }
    parentPort.postMessage({ id, r });
  });
  return;
}

// w는 0.05분 단위까지(팀 최고안 2.45 · 0.75 같은 값이 나오도록). de · dl은 실제 거리 값 사이만 의미가 있다.
const TH = { urg: [0.5], w: [0.25, 0.5, 0.6, 0.65, 0.7, 0.75, 0.8, 0.85, 0.9, 1, 1.25, 1.5, 2, 2.25, 2.35, 2.4, 2.45, 2.5, 2.55, 2.65, 2.8, 3, 3.5, 4, 5, 6.5, 8], de: [0, 25, 50, 65, 90, 115, 140], dl: [25, 50, 65, 90], qo: [0, 1, 2, 3, 4] };
const FEATS = Object.keys(TH);
const DIR = '000011110000000111100000000110101';
let rs = Number(process.argv[5] || 1);
// mulberry32. (처음 쓴 LCG는 곱셈이 2^53을 넘어 정밀도가 깨지고 짧은 주기로 돌아 같은 후보만 나왔다)
const rnd = () => { rs = (rs + 0x6D2B79F5) | 0; let t = Math.imul(rs ^ (rs >>> 15), 1 | rs); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
const pick = a => a[Math.floor(rnd() * a.length)];

function fromCsv(text) {
  const t = { n: 19, nodes: {} };
  for (const l of text.trim().split(/\r?\n/)) {
    const f = l.split(',');
    if (f[0] === 'fleet') t.n = +f[1];
    if (f[0] === 'key') t.nodes[+f[1]] = { f: f[2], d: f[3], th: +f[4], lo: +f[5], hi: +f[6] };
  }
  return t;
}
function toCsv(t) {
  return [`dir,${DIR}`, `fleet,${t.n}`, ...Object.keys(t.nodes).sort((a, b) => a - b).map(id => {
    const x = t.nodes[id]; return `key,${id},${x.f},${x.d},${x.th},${x.lo},${x.hi}`;
  })].join('\n');
}
const clone = t => JSON.parse(JSON.stringify(t));
function reach(t) { const out = [], st = [1]; while (st.length) { const id = st.pop(); if (!id || !t.nodes[id]) continue; out.push(id); st.push(t.nodes[id].lo, t.nodes[id].hi); } return out; }
function prune(t) { const keep = new Set(reach(t)); for (const id of Object.keys(t.nodes)) if (!keep.has(+id)) delete t.nodes[id]; return t; }
function freeId(t) { for (let i = 2; i < 30; i++) if (!t.nodes[i]) return i; }

function mutate(t0) {
  const t = clone(t0), ids = reach(t), id = pick(ids), x = t.nodes[id], k = rnd();
  if (k < 0.30) { // 경계값 바꾸기(이웃 값 우선)
    const g = TH[x.f], i = g.indexOf(x.th);
    x.th = i >= 0 && rnd() < 0.7 ? g[Math.max(0, Math.min(g.length - 1, i + (rnd() < 0.5 ? -1 : 1)))] : pick(g);
  } else if (k < 0.45) { x.f = pick(FEATS); x.th = pick(TH[x.f]); }
  else if (k < 0.55) x.d = x.d === 'min' ? 'max' : 'min';
  else if (k < 0.75) { // 잎 자리에 기준 하나 더하기
    if (ids.length >= 9) return mutate(t0);
    const slots = []; for (const j of ids) { if (!t.nodes[j].lo) slots.push([j, 'lo']); if (!t.nodes[j].hi) slots.push([j, 'hi']); }
    if (!slots.length) return mutate(t0);
    const [j, s] = pick(slots), nid = freeId(t), f = pick(FEATS);
    t.nodes[nid] = { f, d: pick(['min', 'max']), th: pick(TH[f]), lo: 0, hi: 0 }; t.nodes[j][s] = nid;
  } else if (k < 0.87) { // 가지 하나 잘라 내기
    const par = ids.filter(j => (t.nodes[j].lo || t.nodes[j].hi));
    if (!par.length) return mutate(t0);
    const j = pick(par), s = t.nodes[j].lo && (!t.nodes[j].hi || rnd() < 0.5) ? 'lo' : 'hi';
    t.nodes[j][s] = 0; prune(t);
  } else if (k < 0.95) { const s = x.lo; x.lo = x.hi; x.hi = s; }
  else t.n = Math.max(16, Math.min(21, t.n + (rnd() < 0.5 ? -1 : 1)));
  return t;
}

function pool(k) {
  const ws = Array.from({ length: k }, () => new Worker(__filename));
  let seq = 0; const wait = new Map(), q = [], idle = [...ws];
  ws.forEach(w => w.on('message', m => { wait.get(m.id)(m.r); wait.delete(m.id); idle.push(w); pump(); }));
  function pump() { while (idle.length && q.length) idle.pop().postMessage(q.shift()); }
  return { run: (text, seeds) => new Promise(res => { const id = seq++; wait.set(id, res); q.push({ id, text, seeds }); pump(); }), close: () => ws.forEach(w => w.terminate()) };
}
const R = (a, b) => Array.from({ length: b - a + 1 }, (_, i) => a + i);

(async () => {
  const [start, gens, prefix] = [process.argv[2], +process.argv[3], process.argv[4]];
  const P = pool(Number(process.env.W || 13)), TRAIN = R(+(process.env.TRAIN0 || 1001), +(process.env.TRAIN0 || 1001) + +(process.env.NTRAIN || 40) - 1), CHECK = R(3001, 3000 + +(process.env.NTRAIN || 40)), LAMBDA = 26;
  let cur = fromCsv(fs.readFileSync(start, 'utf8'));
  let curV = (await P.run(toCsv(cur), TRAIN)).raw;
  const seen = new Map([[toCsv(cur), curV]]), log = [], t0 = Date.now();
  console.log(`시작 ${start} 학습 ${curV.toFixed(2)}`);
  for (let g = 0; g < gens; g++) {
    const kids = [];
    // 한 걸음 변화는 금방 다 써 버리므로(첫 판에서 같은 자리를 맴돎) 1~4곳을 한꺼번에 바꾼다. 시도 횟수도 막아 둔다.
    for (let tries = 0; kids.length < LAMBDA && tries < 20000; tries++) {
      let m = mutate(cur); const k = 1 + Math.floor(-Math.log(1 - rnd()) / 0.9);
      for (let j = 1; j < Math.min(k, 4); j++) m = mutate(m);
      const c = toCsv(m); if (!seen.has(c)) { seen.set(c, null); kids.push([m, c]); }
    }
    if (g % 5 === 0) console.log(`  (세대 ${g}, 지금 학습 ${curV.toFixed(2)}, ${((Date.now() - t0) / 60000).toFixed(1)}분)`);
    const res = await Promise.all(kids.map(([m, c]) => P.run(c, TRAIN).then(r => ({ m, c, v: r.stable ? r.raw : Infinity }))));
    res.forEach(r => seen.set(r.c, r.v));
    res.sort((a, b) => a.v - b.v);
    if (!res.length) { console.log('  새 후보가 없어 멈춤'); break; }
    if (res[0].v < curV - 0.3) { // 학습에서 좋아지면 확인 시드에서도 좋아져야 받아들인다
      const [a, b] = await Promise.all([P.run(res[0].c, CHECK), P.run(toCsv(cur), CHECK)]);
      const ok = a.stable && a.raw < b.raw;
      log.push({ g, train: res[0].v, check: a.raw, prevCheck: b.raw, ok, csv: res[0].c });
      console.log(`세대 ${g}: 학습 ${curV.toFixed(2)}→${res[0].v.toFixed(2)} | 확인 ${b.raw.toFixed(2)}→${a.raw.toFixed(2)} ${ok ? '받음' : '버림'} (${((Date.now() - t0) / 60000).toFixed(1)}분)`);
      if (ok) { cur = res[0].m; curV = res[0].v; fs.writeFileSync(`${prefix}_cur.csv`, toCsv(cur) + '\n'); }
    }
  }
  fs.writeFileSync(`${prefix}_log.json`, JSON.stringify(log, null, 1));
  console.log(`끝: 학습 ${curV.toFixed(2)}, ${((Date.now() - t0) / 60000).toFixed(1)}분\n${toCsv(cur)}`);
  P.close();
})();
