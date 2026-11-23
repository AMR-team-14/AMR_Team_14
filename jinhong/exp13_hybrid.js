// 실험 13: 두 팀원 안의 장점 합치기.
// w(0) 구분은 「de>180 차 먼저」 규칙 바로 위에만 있으면 된다(도착 순간엔 주문이 하나라 다른 노드는 차 선택에 영향이 없다).
// 그러면 루트를 de로 되돌리고 w(0)을 먼 쪽 가지로 내릴 수 있다. 가까운 쪽 가지에서는 w를 다시 쓸 수 있어 민재안의 긴급 시간 기준(w min 2.45)을 붙일 수 있다.
const fs = require('fs');
const { Engine, specToCsv } = require('./cx.js');
const { raceList, N, fmt, show, toSpec } = require('./lib_race.js');
const { toTree } = require('./race.js'); const { readSpec } = require('./cx.js');
(async () => {
  const eng = new Engine(), base = toTree(readSpec('designs/b2_n21.csv')), c = [];
  const FAR = (wth) => N('w', 'max', wth, N('dl', 'min', 50, N('de', 'max', 180), null), N('urg', 'min', 0.5, N('dl', 'min', 65), null));
  const add = (name, root) => { for (const n of [20, 21, 22]) c.push({ name: `${name} n${n}`, t: { n, root } }); };
  for (const r of [50, 65]) {
    add(`H0 de${r} 루트, 시간 기준 없음`, N('de', 'min', r, N('qo', 'max', 1, null, N('urg', 'max', 0.5)), FAR(0.001)));
    for (const w of [1.5, 2, 2.45, 3, 3.5]) add(`H1 de${r} 루트 + 가까운 긴급 w≤${w}`, N('de', 'min', r, N('qo', 'max', 1, null, N('urg', 'max', 0.5, null, N('w', 'min', w))), FAR(0.001)));
    for (const w of [1.5, 2.45, 3.5]) add(`H2 de${r} 루트 + 가까운 쪽 전체 w≤${w}`, N('de', 'min', r, N('qo', 'max', 1, N('w', 'min', w), N('urg', 'max', 0.5)), FAR(0.001)));
  }
  const r = await raceList(eng, base, c, { stages: [[20, 6], [100, null]] });
  const out = [`기준 B2(w 루트 8노드 21대) 500시드 평균 ${r.baseMean.toFixed(2)}`];
  r.final.forEach(x => out.push(`${fmt(x)}  ${x.name}`)); out.push(`100시드에서 탈락 ${r.dropped.length}개`);
  console.log(out.join('\n')); fs.writeFileSync('runs/exp13_hybrid.txt', out.join('\n') + '\n');
  if (r.final[0]) fs.writeFileSync('designs/exp13_best.csv', specToCsv(toSpec(r.final[0].t)));
  eng.close();
})();
