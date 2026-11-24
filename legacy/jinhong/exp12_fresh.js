// 실험 12: 도착 가지(주문이 들어온 순간 어느 빈 차를 보낼까) 전수 훑기.
// 도착 순간에는 주문이 하나뿐이라 urg · dl 노드는 주문 종류를 가르는 역할만 하고, 차를 고르는 건 de(max, X)뿐이다
// (de(min)은 기본 동점 규칙인 「가까운 차」와 같아 효과가 없다). 그래서 「주문 종류 → X」 조합을 모두 만든다.
const fs = require('fs');
const { Engine, specToCsv } = require('./cx.js');
const { raceList, N, fmt, show, toSpec } = require('./lib_race.js');
const OLD = () => N('de', 'min', 65, N('qo', 'max', 1, null, N('urg', 'max', 0.5)), N('urg', 'min', 0.5));
const X = [null, 0, 25, 50, 65, 90, 115, 140, 155, 180], TH = [25, 50, 65, 90];
const de = x => x === null ? null : N('de', 'max', x);
(async () => {
  const eng = new Engine(), base = { n: 21, root: N('w', 'max', 0.001, N('dl', 'min', 50, de(180), null), OLD()) }, c = [], seen = new Set();
  const add = (name, fresh) => { const t = { n: 21, root: N('w', 'max', 0.001, fresh, OLD()) }, k = show(t.root); if (!seen.has(k)) { seen.add(k); c.push({ name, t }); } };
  for (const x of X) add(`전체 de>${x}`, de(x));
  for (const th of TH) for (const a of X) for (const b of X) if (a !== null || b !== null) add(`dl≤${th}: de>${a} / dl>${th}: de>${b}`, N('dl', 'min', th, de(a), de(b)));
  for (const a of X) for (const b of X) if (a !== null || b !== null) add(`일반: de>${a} / 긴급: de>${b}`, N('urg', 'min', 0.5, de(a), de(b)));
  for (const th of TH) for (const a of X) for (const b of X) for (const u of [null, 65, 90, 115]) if (a !== null || b !== null)
    add(`일반[dl≤${th}: de>${a} / dl>${th}: de>${b}] 긴급: de>${u}`, N('urg', 'min', 0.5, N('dl', 'min', th, de(a), de(b)), de(u)));
  console.log('후보', c.length);
  const t0 = Date.now();
  const r = await raceList(eng, base, c, { stages: [[4, 4], [16, 1.5], [60, 0.5], [120, null]] });
  const out = [`기준(w 0.001 · dl≤50이면 de>180 · 21대) 600시드 평균 ${r.baseMean.toFixed(2)} | 후보 ${c.length}개, ${((Date.now() - t0) / 60).toFixed(0)}초...`];
  r.final.slice(0, 25).forEach(x => out.push(`${fmt(x)}  ${x.name}`));
  out.push(`끝까지 남은 후보 ${r.final.length}개`);
  console.log(out.join('\n')); fs.writeFileSync('runs/exp12_fresh.txt', out.join('\n') + '\n');
  fs.writeFileSync('runs/exp12_fresh_top.json', JSON.stringify(r.final.slice(0, 40).map(x => ({ name: x.name, d: x.p.d, se: x.p.se, t: x.p.t, spec: toSpec(x.t) })), null, 1));
  if (r.final[0]) fs.writeFileSync('designs/exp12_best.csv', specToCsv(toSpec(r.final[0].t)));
  eng.close();
})();
