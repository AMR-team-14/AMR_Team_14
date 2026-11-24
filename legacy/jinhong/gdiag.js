// 배차 진단(케이스 일반판): 웹 원본 엔진의 로그로 「긴급 / 일반 주문이 어떤 차를 받았나」를 본다.
//  - 배차 순간의 빈 차 거리 de 분포(0m = 출발지에 서 있던 차), 배차 전 대기(주문이 차를 기다린 시간), 픽업까지 대기
// 사용: node gdiag.js <케이스 번호> <csv...>
const fs = require('fs'), path = require('path');
const { loadCase } = require('./gx.js');
const W = require(path.join(__dirname, 'web', 'engine_final.js'));
const [c, ...files] = process.argv.slice(2), P = loadCase(c);
const q = (a, p) => a.length ? a.slice().sort((x, y) => x - y)[Math.floor(p * (a.length - 1))] : NaN, avg = a => a.length ? a.reduce((s, x) => s + x, 0) / a.length : NaN;
for (const f of files) {
  const ps = W.agvParse_(P, fs.readFileSync(f, 'utf8')); if (!ps.design) { console.log(f, ps.errs); continue; }
  const net = W.agvNet_(P, ps.design.dirs), G = { u: { de: [], qw: [], w: [] }, n: { de: [], qw: [], w: [] } }, byO = {};
  for (const seed of P.seeds) {
    const disp = new Map(); let now = 0;
    const s = W.agvSim_(P, net, ps.design, seed, (ev, v, r, x) => {
      if (ev === 'disp') disp.set(r.id, { de: x, t: s.t });
      else if (ev === 'pick' && r.t >= P.warm) {
        const d = disp.get(r.id), g = r.d === P.urgentDest ? G.u : G.n;
        g.de.push(d.de); g.qw.push(d.t - r.t); g.w.push(x);
        if (r.d === P.urgentDest) { const k = P.depts[r.o]; (byO[k] = byO[k] || []).push(x); }
      }
    });
    while (s.step()) { } void now;
  }
  console.log(`\n${path.basename(f)} (대수 ${ps.design.n})`);
  for (const [nm, g] of [['긴급', G.u], ['일반', G.n]]) {
    const z = g.de.filter(x => x === 0).length / g.de.length, near = g.de.filter(x => x > 0 && x <= 50).length / g.de.length, im = g.qw.filter(x => x <= 1e-9).length / g.qw.length;
    console.log(`  ${nm} ${g.de.length}건: 출발지에 있던 차 ${(100 * z).toFixed(1)}% · 50m 이내 ${(100 * near).toFixed(1)}% · 평균 de ${avg(g.de).toFixed(1)}m | 즉시 배차 ${(100 * im).toFixed(1)}% · 배차 전 대기 평균 ${avg(g.qw).toFixed(3)}분 | 픽업까지 평균 ${avg(g.w).toFixed(3)} · p95 ${q(g.w, 0.95).toFixed(3)}분`);
  }
  console.log('  긴급 출발지별 p95:', Object.entries(byO).map(([k, a]) => `${k} ${a.length}건 평균 ${avg(a).toFixed(2)} p95 ${q(a, 0.95).toFixed(2)}`).join(' | '));
}
