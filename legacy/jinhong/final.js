// 최종 검증: 탐색에 한 번도 쓰지 않은 시드 5001~5100(100개)과 공개 시드 11~15로 후보를 기준안과 비교한다.
// 같은 시드에서 짝지어 비교하고, 5시드 묶음 20개 중 기준안보다 낮은 묶음 수도 센다.
// 사용: node final.js <기준 csv> <후보 csv...>
const { Worker, isMainThread, parentPort } = require('worker_threads');
const fs = require('fs');
if (!isMainThread) {
  const { runSeed } = require('./fast.js'); const { parse } = require('./eval.js');
  parentPort.on('message', ({ id, text, seed }) => {
    const o = runSeed(parse(text), seed);
    parentPort.postMessage({ id, o: { ws: o.ws, wu: o.wu, unstable: o.unstable } });
  });
  return;
}
const { pooled } = require('./fast.js'); const { parse } = require('./eval.js');
const ws = Array.from({ length: 13 }, () => new Worker(__filename)); let seq = 0; const wait = new Map(), q = [], idle = [...ws];
ws.forEach(w => w.on('message', m => { wait.get(m.id)(m.o); wait.delete(m.id); idle.push(w); pump(); }));
function pump() { while (idle.length && q.length) idle.pop().postMessage(q.shift()); }
const run = (text, seed) => new Promise(res => { const id = seq++; wait.set(id, res); q.push({ id, text, seed }); pump(); });
const R = (a, b) => Array.from({ length: b - a + 1 }, (_, i) => a + i);
(async () => {
  const V0 = +(process.env.VAL0 || 5001), NV = +(process.env.NVAL || 100), files = process.argv.slice(2), VAL = R(V0, V0 + NV - 1), PUB = R(11, 15);
  const all = await Promise.all(files.map(async f => {
    const text = fs.readFileSync(f, 'utf8'), n = parse(text).n;
    const [val, pub] = await Promise.all([Promise.all(VAL.map(s => run(text, s))), Promise.all(PUB.map(s => run(text, s)))]);
    return { f: f.split(/[\\/]/).pop(), n, val, pub };
  }));
  const base = all[0], blocks = x => Array.from({ length: NV / 5 }, (_, b) => pooled(x.n, x.val.slice(b * 5, b * 5 + 5)).raw);
  const bb = blocks(base);
  for (const x of all) {
    const v = pooled(x.n, x.val), p = pooled(x.n, x.pub), bl = blocks(x);
    const win = bl.filter((c, i) => c < bb[i]).length, diff = bl.map((c, i) => c - bb[i]);
    const md = diff.reduce((s, d) => s + d, 0) / bl.length, se = Math.sqrt(diff.reduce((s, d) => s + (d - md) ** 2, 0) / (bl.length - 1) / bl.length);
    console.log(`${x.f.padEnd(28)} n${x.n} | 공개 ${p.value.toFixed(2)} | 검증${NV} ${v.value.toFixed(2)} (W̄ ${v.mean.toFixed(3)}, U95 ${v.u95.toFixed(3)}) | 기준 대비 ${md >= 0 ? '+' : ''}${md.toFixed(2)} ± ${se.toFixed(2)}, 묶음 ${win}/${bl.length} 낮음`);
  }
  ws.forEach(w => w.terminate());
})();
