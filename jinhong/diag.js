// 진단: 배차를 「도착 즉시(빈 차 있음)」와 「대기 후(차가 비었을 때)」로 나눠, 출발 부서별 대기 · 빈 차 거리 · 차 위치를 본다.
// 사용: node diag.js <csv> [seed0] [시드 수]
const { Engine, readSpec } = require('./cx.js');
(async () => {
  const [f, s0 = 100001, cnt = 40] = process.argv.slice(2), eng = new Engine(), d = await eng.diag(readSpec(f), +s0, +cnt), tot = d.total;
  let fr = 0, frw = 0, ow = 0, deAll = 0;
  for (const x of d.byOrigin) { deAll += x.de * x.cnt; if (x.fresh) { fr += x.cnt; frw += x.w * x.cnt; } else ow += x.w * x.cnt; }
  console.log(`${f} n${d.n} | 평균 대기 ${((frw + ow) / tot).toFixed(3)}분, 빈 차 평균 거리 ${(deAll / tot).toFixed(1)}m`);
  console.log(`도착 즉시 ${(100 * fr / tot).toFixed(1)}% (평균 대기 ${(frw / fr).toFixed(2)}) | 대기 후 ${(100 - 100 * fr / tot).toFixed(1)}% (평균 대기 ${(ow / (tot - fr)).toFixed(2)})`);
  console.log('출발 부서  [도착 즉시] 비중 · 대기 · 빈 차 거리   |   [대기 후] 비중 · 대기 · 배정 전 대기 · 빈 차 거리');
  for (let o = 0; o < 7; o++) {
    const a = d.byOrigin.find(x => x.fresh && x.o === o), b = d.byOrigin.find(x => !x.fresh && x.o === o); if (!a && !b) continue;
    console.log('  ' + d.depts[o].padEnd(7), a ? `${(100 * a.cnt / tot).toFixed(1).padStart(5)}% ${a.w.toFixed(2)}분 ${a.de.toFixed(0).padStart(3)}m` : ''.padEnd(18), '  |  ', b ? `${(100 * b.cnt / tot).toFixed(1).padStart(5)}% ${b.w.toFixed(2)}분 (배정 전 ${b.qw.toFixed(2)}) ${b.de.toFixed(0).padStart(3)}m` : '');
  }
  for (const k of [1, 0]) {
    console.log(k ? '도착 즉시: 차 위치(행) → 출발 부서(열), 전체 대비 %' : '대기 후: 차 위치(행) → 출발 부서(열), 전체 대비 %');
    console.log('         ' + d.depts.map(x => x.padStart(7)).join(''));
    d.pair[k].forEach((row, v) => console.log('  ' + d.depts[v].padEnd(7) + row.map(c => (100 * c / tot).toFixed(1).padStart(7)).join('')));
  }
  const u = d.urg; console.log(`긴급: 평균 ${u.mean.toFixed(2)}분, 도착 즉시 비중 ${(100 * u.freshShare).toFixed(0)}% | 상위 5%(≥ ${u.top5.min.toFixed(2)}분): 도착 즉시 ${(100 * u.top5.freshShare).toFixed(0)}%, 빈 차 거리 ${u.top5.de.toFixed(0)}m, 배정 전 대기 ${u.top5.qw.toFixed(2)}분, 차 위치 ${JSON.stringify(u.top5.byLoc)}`);
  eng.close();
})();
