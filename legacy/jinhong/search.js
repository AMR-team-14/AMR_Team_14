// 팀 최고안(robust_best_19)의 거의 쓰이지 않는 두 자리(#4, #7)를 다른 기준으로 바꿔 본다.
// 고르는 기준은 학습 시드(101~120) 비용, 뽑힌 후보는 검증 시드(151~200)와 공개 시드(11~15)로 다시 본다.
// 웹 채점 코드(web/engine_raw.js)를 그대로 쓰고, 워커 스레드로 나눠 돌린다.
const { Worker, isMainThread, parentPort, workerData } = require('worker_threads');
const fs = require('fs');

if (!isMainThread) {
  const { evalText } = require('./eval.js');
  parentPort.on('message', ({ id, text, seeds }) => {
    let r;
    try { r = evalText(text, seeds); } catch (e) { r = { stable: false }; } // 한 갈래에 같은 기준 중복 등 규칙 위반
    parentPort.postMessage({ id, v: r.stable ? r.value : Infinity, mean: r.mean, u95: r.u95 });
  });
  return;
}

const BASE = fs.readFileSync(__dirname + '/designs/robust_best_19.csv', 'utf8').trim().split(/\r?\n/);
const TH = { urg: [0.5], w: [0.5, 1, 1.5, 2, 3, 5], de: [25, 50, 65, 90], dl: [25, 50, 65, 90], qo: [0, 1, 2, 3] };
function cands() {
  const out = [];
  for (const f of Object.keys(TH)) for (const dir of ['min', 'max']) for (const t of TH[f]) out.push([f, dir, t]);
  return out;
}
function setRow(lines, id, f, dir, t) {
  return lines.map(l => l.startsWith(`key,${id},`) ? `key,${id},${f},${dir},${t},0,0` : l);
}
function text(lines, n) { return lines.map(l => l.startsWith('fleet,') ? `fleet,${n}` : l).join('\n'); }

function pool(k) {
  const ws = Array.from({ length: k }, () => new Worker(__filename));
  let seq = 0; const wait = new Map(); const q = []; const idle = [...ws];
  ws.forEach(w => w.on('message', m => { wait.get(m.id)(m); wait.delete(m.id); idle.push(w); pump(); }));
  function pump() { while (idle.length && q.length) { const w = idle.pop(); w.postMessage(q.shift()); } }
  return {
    run: (text, seeds) => new Promise(res => { const id = seq++; wait.set(id, res); q.push({ id, text, seeds }); pump(); }),
    close: () => ws.forEach(w => w.terminate()),
  };
}
const R = (a, b) => Array.from({ length: b - a + 1 }, (_, i) => a + i);

(async () => {
  const P = pool(12), TRAIN = R(101, 120), TEST = R(151, 200), PUB = R(11, 15);
  const jobs = [];
  for (const n of [18, 19]) {
    jobs.push({ name: `기준 n${n}`, lines: BASE, n });
    for (const slot of [4, 7]) for (const [f, d, t] of cands())
      jobs.push({ name: `n${n} #${slot}=${f} ${d} ${t}`, lines: setRow(BASE, slot, f, d, t), n });
  }
  const t0 = Date.now();
  const r1 = await Promise.all(jobs.map(j => P.run(text(j.lines, j.n), TRAIN).then(m => ({ ...j, train: m.v }))));
  r1.sort((a, b) => a.train - b.train);
  console.log(`1단계 ${jobs.length}개, ${((Date.now() - t0) / 1000).toFixed(0)}초. 학습 시드 상위 10:`);
  r1.slice(0, 10).forEach(j => console.log(`  ${j.name}: ${j.train.toFixed(2)}`));
  ['기준 n18', '기준 n19'].forEach(b => console.log(`  ${b}: ${r1.find(j => j.name === b).train.toFixed(2)}`));

  // 2단계: 자리별 상위 3개를 서로 조합
  const combos = [];
  for (const n of [18, 19]) {
    const top = s => r1.filter(j => j.n === n && j.name.includes(`#${s}=`)).slice(0, 3);
    for (const a of top(4)) for (const b of top(7)) {
      const [fa, da, ta] = a.name.split('=')[1].split(' '), [fb, db, tb] = b.name.split('=')[1].split(' ');
      combos.push({ name: `n${n} #4=${fa} ${da} ${ta} + #7=${fb} ${db} ${tb}`, lines: setRow(setRow(BASE, 4, fa, da, ta), 7, fb, db, tb), n });
    }
  }
  const r2 = await Promise.all(combos.map(j => P.run(text(j.lines, j.n), TRAIN).then(m => ({ ...j, train: m.v }))));
  const pick = [...r1.filter(j => j.name.startsWith('기준')), ...[...r1, ...r2].filter(j => !j.name.startsWith('기준')).sort((a, b) => a.train - b.train).slice(0, 8)];

  // 3단계: 뽑힌 후보를 검증 시드 50개 + 공개 시드로
  const r3 = await Promise.all(pick.map(async j => {
    const [te, pu] = await Promise.all([P.run(text(j.lines, j.n), TEST), P.run(text(j.lines, j.n), PUB)]);
    return { ...j, test: te.v, pub: pu.v, tW: te.mean, tU: te.u95 };
  }));
  r3.sort((a, b) => a.test - b.test);
  console.log(`\n검증(시드 151~200, 50개) 순:`);
  r3.forEach(j => console.log(`  ${j.name.padEnd(48)} 학습 ${j.train.toFixed(2)} | 검증 ${j.test.toFixed(2)} (W̄ ${j.tW.toFixed(3)}, U95 ${j.tU.toFixed(3)}) | 공개 ${j.pub.toFixed(2)}`));
  fs.writeFileSync(__dirname + '/search_result.json', JSON.stringify({ r1, r2, r3 }, null, 1));
  const best = r3[0]; fs.writeFileSync(__dirname + '/designs/search_best.csv', text(best.lines, best.n) + '\n');
  console.log(`\n최고: ${best.name} → designs/search_best.csv, 총 ${((Date.now() - t0) / 1000).toFixed(0)}초`);
  P.close();
})();
