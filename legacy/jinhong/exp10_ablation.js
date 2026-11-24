// 실험 10: 승민 w 루트 21대안 해부. 어느 노드가 점수를 만드는지(가지 제거), 루트 경계값, 대수.
const fs = require('fs');
const { Engine } = require('./cx.js');
const { raceList, N, fmt, show } = require('./lib_race.js');
const OLD = () => N('de', 'min', 65, N('qo', 'max', 1, null, N('urg', 'max', 0.5)), N('urg', 'min', 0.5));
const FRESH = () => N('dl', 'min', 50, N('de', 'max', 180, N('qo', 'min', 1), N('qo', 'max', 1)), null);
const W = (th, fresh, old) => N('w', 'max', th, fresh, old);
(async () => {
  const eng = new Engine(), base = { n: 21, root: W(0.05, FRESH(), OLD()) }, c = [];
  const add = (name, n, root) => c.push({ name, t: { n, root } });
  add('도착 가지 없음(가까운 차)', 21, W(0.05, null, OLD()));
  add('도착 가지: dl50[de max180]만', 21, W(0.05, N('dl', 'min', 50, N('de', 'max', 180), null), OLD()));
  add('도착 가지: de max180만', 21, W(0.05, N('de', 'max', 180), OLD()));
  add('대기 가지 없음(가까운 것)', 21, W(0.05, FRESH(), null));
  add('대기 가지: de65만', 21, W(0.05, FRESH(), N('de', 'min', 65)));
  add('대기 가지: urg min 제거', 21, W(0.05, FRESH(), N('de', 'min', 65, N('qo', 'max', 1, null, N('urg', 'max', 0.5)), null)));
  add('대기 가지: qo·urg max 제거', 21, W(0.05, FRESH(), N('de', 'min', 65, null, N('urg', 'min', 0.5))));
  for (const th of [0.001, 0.02, 0.1, 0.3, 1]) add(`루트 w ${th}`, 21, W(th, FRESH(), OLD()));
  for (const n of [19, 20, 22, 23]) add(`대수 ${n}`, n, W(0.05, FRESH(), OLD()));
  for (const n of [19, 20, 21]) add(`도착 가지 없음 + 대수 ${n}`, n, W(0.05, null, OLD()));
  const r = await raceList(eng, base, c, { stages: [[100, null]], qcut: 0 });
  const out = [`기준(승민 w 루트 21대) 500시드 평균 ${r.baseMean.toFixed(2)}`];
  for (const x of [...r.final, ...r.dropped]) out.push(`${x.name.padEnd(28)} ${x.ok ? fmt(x) : '반려 발생'}`);
  console.log(out.join('\n')); fs.writeFileSync('runs/exp10_ablation.txt', out.join('\n') + '\n');
  eng.close();
})();
