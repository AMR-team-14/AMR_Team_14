// 실험 23 (자문자답 4회전): 도착 가지 규칙을 22~24대에서 다시 전수로 본다(실험 12는 21대에서만 봤다).
// 가설: 형식 밖 실험(22)에서 대수가 늘수록 좋아졌으므로, 형식 안에서도 대수를 늘리면 「먼 차 먼저」 계열 규칙의 손익이 바뀔 수 있다.
const fs = require('fs');
const { Engine, specToCsv, readSpec } = require('./cx.js');
const { raceList, N, fmt, show, toSpec } = require('./lib_race.js');
const { toTree } = require('./race.js');
const OLD = () => N('de', 'min', 65, N('qo', 'max', 1, null, N('urg', 'max', 0.5)), N('urg', 'min', 0.5, N('dl', 'min', 65), null));
const X = [null, 0, 25, 50, 65, 90, 115, 140, 155, 180], TH = [25, 50, 65, 90];
const de = x => x === null ? null : N('de', 'max', x);
(async () => {
  const eng = new Engine(), base = toTree(readSpec('designs/b2_n21.csv')), c = [], seen = new Set();
  const add = (name, fresh) => { for (const n of [22, 23, 24]) { const t = { n, root: N('w', 'max', 0.001, fresh, OLD()) }, k = n + show(t.root); if (!seen.has(k)) { seen.add(k); c.push({ name: `n${n} ${name}`, t }); } } };
  for (const x of X) add(`전체 de>${x}`, de(x));
  for (const th of TH) for (const a of X) for (const b of X) if (a !== null || b !== null) add(`dl≤${th}: de>${a} / dl>${th}: de>${b}`, N('dl', 'min', th, de(a), de(b)));
  for (const a of X) for (const b of X) if (a !== null || b !== null) add(`일반: de>${a} / 긴급: de>${b}`, N('urg', 'min', 0.5, de(a), de(b)));
  for (const th of TH) for (const a of X) for (const b of X) for (const u of [null, 90]) if (a !== null || b !== null)
    add(`일반[dl≤${th}: de>${a} / dl>${th}: de>${b}] 긴급: de>${u}`, N('urg', 'min', 0.5, N('dl', 'min', th, de(a), de(b)), de(u)));
  console.log('후보', c.length); const t0 = Date.now();
  const r = await raceList(eng, base, c, { stages: [[2, 6], [8, 2], [24, 0.5], [60, null]], qcut: 130 });
  const out = [`기준 B2(21대) 300시드 평균 ${r.baseMean.toFixed(2)} | 후보 ${c.length}개, ${((Date.now() - t0) / 60000).toFixed(1)}분`];
  r.final.slice(0, 15).forEach(x => out.push(`${fmt(x)}  ${x.name}`)); out.push(`끝까지 남은 후보 ${r.final.length}개`);
  console.log(out.join('\n')); fs.writeFileSync('runs/exp23_fresh_n.txt', out.join('\n') + '\n');
  if (r.final[0]) fs.writeFileSync('designs/exp23_best.csv', specToCsv(toSpec(r.final[0].t)));
  eng.close();
})();
