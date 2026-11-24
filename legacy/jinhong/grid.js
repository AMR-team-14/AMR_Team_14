// 두 노드의 경계값을 격자로 훑는다. 같은 시드로 짝지어 기준안과의 차이를 낸다(공통 난수).
// 사용: node grid.js <기준 csv> <노드id>:<값,값,...> <노드id>:<값,...> [시작시드 개수]
// 예:   node grid.js designs/team_1185.csv 6:2.3,2.45,2.6 8:0.65,0.75,0.85 6001 300
const { Worker, isMainThread, parentPort } = require('worker_threads');
const fs = require('fs');
if (!isMainThread) {
  const { runSeed } = require('./fast.js'); const { parse } = require('./eval.js');
  parentPort.on('message', ({ id, text, seed }) => { const o = runSeed(parse(text), seed); parentPort.postMessage({ id, o: { ws: o.ws, wu: o.wu, unstable: o.unstable } }); });
  return;
}
const { pooled } = require('./fast.js'); const { parse } = require('./eval.js');
const ws = Array.from({ length: 13 }, () => new Worker(__filename)); let seq = 0; const wait = new Map(), q = [], idle = [...ws];
ws.forEach(w => w.on('message', m => { wait.get(m.id)(m.o); wait.delete(m.id); idle.push(w); pump(); }));
function pump() { while (idle.length && q.length) idle.pop().postMessage(q.shift()); }
const run = (text, seed) => new Promise(res => { const id = seq++; wait.set(id, res); q.push({ id, text, seed }); pump(); });

const [base, a, b, s0 = 6001, ns = 300] = process.argv.slice(2);
const [ia, va] = a.split(':'), [ib, vb] = b.split(':');
const lines = fs.readFileSync(base, 'utf8').trim().split(/\r?\n/);
const setTh = (ls, id, v) => ls.map(l => { const f = l.split(','); if (f[0] === 'key' && f[1] === id) { f[4] = v; return f.join(','); } return l; });
const SEEDS = Array.from({ length: +ns }, (_, i) => +s0 + i);
(async () => {
  const designs = [];
  for (const x of va.split(',')) for (const y of vb.split(',')) designs.push({ x, y, text: setTh(setTh(lines, ia, x), ib, y).join('\n') });
  const t0 = Date.now();
  const res = await Promise.all(designs.map(async d => {
    const outs = await Promise.all(SEEDS.map(s => run(d.text, s)));
    return { ...d, n: parse(d.text).n, outs };
  }));
  const ref = res.find(r => r.text === lines.join('\n')) || res[0];
  const blk = r => Array.from({ length: SEEDS.length / 5 }, (_, k) => pooled(r.n, r.outs.slice(k * 5, k * 5 + 5)).raw);
  const rb = blk(ref);
  const rows = res.map(r => {
    const v = pooled(r.n, r.outs), bl = blk(r), d = bl.map((c, i) => c - rb[i]);
    const md = d.reduce((s, x) => s + x, 0) / d.length, se = Math.sqrt(d.reduce((s, x) => s + (x - md) ** 2, 0) / (d.length - 1) / d.length);
    return { x: r.x, y: r.y, v: v.raw, md, se };
  }).sort((p, q) => p.v - q.v);
  console.log(`기준 #${ia}=${ref.x}, #${ib}=${ref.y} | 시드 ${s0}~${+s0 + +ns - 1} | ${designs.length}개, ${((Date.now() - t0) / 1000).toFixed(0)}초`);
  rows.slice(0, 12).forEach(r => console.log(`  #${ia}=${r.x} #${ib}=${r.y}: ${r.v.toFixed(2)} (기준 대비 ${r.md >= 0 ? '+' : ''}${r.md.toFixed(2)} ± ${r.se.toFixed(2)})`));
  ws.forEach(w => w.terminate());
})();
