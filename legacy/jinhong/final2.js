// 최종 판정: 후보를 팀 최고안 둘(민재 1185안, 승민 w 루트 21대안)과 같은 시드에서 짝비교한다.
//  - 시드 구간은 인자로 고른다. 확인용 200001~(탐색 중간 점검), 최종용 300001~(마지막에 한 번만).
//  - 공개 점수(시드 11~15 = 리더보드)는 C++ 엔진과 웹 원본 JS 양쪽으로 계산해 같은지 본다.
// 사용: node final2.js <seed0> <시드 수> <후보 csv...>
const fs = require('fs');
const E = require('./web/engine_raw.js');
const { parse } = require('./eval.js');
const { Engine, readSpec, paired, mean } = require('./cx.js');
(async () => {
  const [seed0, count, ...files] = process.argv.slice(2);
  const base = ['designs/team_1185.csv', 'designs/jsm_wroot_21.csv'], all = [...base, ...files];
  const eng = new Engine();
  const res = await eng.run(all.map(f => ({ ...readSpec(f), seed0: +seed0, count: +count, block: 5 })));
  const pub = await eng.run(all.map(f => ({ ...readSpec(f), seed0: 11, count: 5, block: 5 })));
  console.log(`시드 ${seed0}~${+seed0 + +count - 1} (${count}개, 5시드 묶음 ${count / 5}개)`);
  all.forEach((f, i) => {
    const c = res[i], bad = c.filter(x => !Number.isFinite(x)).length, ok = c.filter(Number.isFinite);
    const web = E.agvEvaluate_(E.DATA_AGV, parse(fs.readFileSync(f, 'utf8')), [11, 12, 13, 14, 15]);
    const sd = Math.sqrt(ok.reduce((s, x) => s + (x - mean(ok)) ** 2, 0) / (ok.length - 1));
    let line = `${f.split('/').pop().padEnd(26)} n${readSpec(f).n} | 평균 ${mean(ok).toFixed(2)} (묶음 표준편차 ${sd.toFixed(1)}, 최악 ${Math.max(...ok).toFixed(0)}, 반려 묶음 ${bad}) | 공개 ${web.stable ? web.value.toFixed(2) : '반려'}${web.stable && Math.abs(web.value - pub[i][0]) > 0.006 ? ' (C++ ' + pub[i][0].toFixed(2) + ' 불일치!)' : ''}`;
    if (i >= 2 && !bad) for (const b of [0, 1]) { const p = paired(c, res[b]); line += ` | vs ${b ? '승민' : '민재'} ${p.d >= 0 ? '+' : ''}${p.d.toFixed(2)} ± ${p.se.toFixed(2)} (t ${p.t.toFixed(1)}, ${p.wins}/${p.n})`; }
    console.log(line);
  });
  eng.close();
})();
