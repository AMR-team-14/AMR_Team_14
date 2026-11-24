// 실험 11: 루트 w 경계값을 0 근처까지 훑는다. 쓸모없던 qo 노드 2개는 뺀 7노드 나무 기준.
const fs = require('fs');
const { Engine } = require('./cx.js');
const { raceList, N, fmt } = require('./lib_race.js');
const OLD = () => N('de', 'min', 65, N('qo', 'max', 1, null, N('urg', 'max', 0.5)), N('urg', 'min', 0.5));
const FRESH = () => N('dl', 'min', 50, N('de', 'max', 180), null);
const W = (th, fresh, old) => N('w', 'max', th, fresh, old);
(async () => {
  const eng = new Engine(), base = { n: 21, root: W(0.05, FRESH(), OLD()) }, c = [];
  for (const th of [0, 0.0001, 0.001, 0.003, 0.01, 0.02]) for (const n of [20, 21, 22]) c.push({ name: `w ${th} n${n}`, t: { n, root: W(th, FRESH(), OLD()) } });
  const r = await raceList(eng, base, c, { stages: [[100, null]], qcut: 0 });
  const out = [`기준(w 0.05, 7노드, 21대) 500시드 평균 ${r.baseMean.toFixed(2)}`];
  for (const x of [...r.final, ...r.dropped]) out.push(`${x.name.padEnd(18)} ${x.ok ? fmt(x) : '반려 발생'}`);
  console.log(out.join('\n')); fs.writeFileSync('runs/exp11_root.txt', out.join('\n') + '\n');
  eng.close();
})();
