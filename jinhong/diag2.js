// diag2 — 비용이 어디서 나오나(밤샘 루프 진단). 웹 원본 엔진 로그로 긴급 대기 꼬리(상위 5% 부근)를 분해한다.
//  대기 = 배차 전 대기(qw: 빈 차가 없거나 순서에서 밀림) + 빈 차 이동(de/ve) + 교차로 지연
// 사용: node diag2.js <케이스> <csv...>
const fs = require('fs'), path = require('path');
const { loadCase } = require('./gx.js');
const W = require(path.join(__dirname, 'web', 'engine_final.js'));
const [c, ...files] = process.argv.slice(2), P = loadCase(c);
const avg = a => a.length ? a.reduce((s, x) => s + x, 0) / a.length : NaN;
const q = (a, p) => a.length ? a.slice().sort((x, y) => x - y)[Math.floor(p * (a.length - 1))] : NaN;
const fac = 60 / P.at;
for (const f of files) {
  const ps = W.agvParse_(P, fs.readFileSync(f, 'utf8')); if (!ps.design) { console.log(f, ps.errs); continue; }
  const net = W.agvNet_(P, ps.design.dirs), rows = [];
  for (const seed of P.seeds) {
    const disp = new Map();
    const s = W.agvSim_(P, net, ps.design, seed, (ev, v, r, x) => {
      if (ev === 'disp') disp.set(r.id, { de: x, t: s.t });
      else if (ev === 'pick' && r.t >= P.warm) { const d = disp.get(r.id); rows.push({ o: r.o, d: r.d, u: r.d === P.urgentDest, w: x, qw: d.t - r.t, de: d.de, jd: x - (d.t - r.t) - d.de / P.ve * fac }); }
    });
    while (s.step()) { }
  }
  const lam = P.alpha * P.flows.reduce((s, x) => s + x[2], 0), U = rows.filter(r => r.u), mw = avg(rows.map(r => r.w)), p95 = q(U.map(r => r.w), 0.95);
  console.log(`\n${path.basename(f)} 대수 ${ps.design.n}: 대수 ${P.cAGV * ps.design.n} + 평균 대기 ${(P.cW * lam * mw).toFixed(2)}(${mw.toFixed(3)}분) + 긴급 p95 ${(P.cU * p95).toFixed(2)}(${p95.toFixed(3)}분) = ${(P.cAGV * ps.design.n + P.cW * lam * mw + P.cU * p95).toFixed(2)}`);
  const part = (nm, a) => console.log(`  ${nm.padEnd(18)} ${String(a.length).padStart(5)}건 | 대기 ${avg(a.map(r => r.w)).toFixed(2)} = 배차 전 ${avg(a.map(r => r.qw)).toFixed(2)} + 이동 ${(avg(a.map(r => r.de)) / P.ve * fac).toFixed(2)}(de ${avg(a.map(r => r.de)).toFixed(0)}m) + 교차로 ${avg(a.map(r => r.jd)).toFixed(2)} | 즉시 배차 ${(100 * a.filter(r => r.qw <= 1e-9).length / a.length).toFixed(0)}% · p95 ${q(a.map(r => r.w), 0.95).toFixed(2)}`);
  part('전체', rows); part('일반', rows.filter(r => !r.u)); part('긴급', U);
  const lo = q(U.map(r => r.w), 0.90), hi = q(U.map(r => r.w), 0.99), band = U.filter(r => r.w >= lo && r.w <= hi);
  part('긴급 90~99% 꼬리', band);
  const os = [...new Set(U.map(r => r.o))].sort((a, b) => a - b);
  for (const o of os) { const a = U.filter(r => r.o === o); part(`  긴급 ${P.depts[o]}`, a); console.log(`      꼬리(90~99%)에서 차지 ${(100 * band.filter(r => r.o === o).length / band.length).toFixed(0)}% (주문 비중 ${(100 * a.length / U.length).toFixed(0)}%)`); }
  const no = [...new Set(rows.filter(r => !r.u).map(r => r.o))].sort((a, b) => a - b);
  console.log('  일반 출발지별 평균 대기: ' + no.map(o => { const a = rows.filter(r => !r.u && r.o === o); return `${P.depts[o]} ${a.length}건 ${avg(a.map(r => r.w)).toFixed(2)}(전 ${avg(a.map(r => r.qw)).toFixed(2)}, de ${avg(a.map(r => r.de)).toFixed(0)})`; }).join(' | '));
  // 꼬리의 원인 분류: 배차 전 대기가 절반 이상 / 먼 차(de 상위) / 교차로
  const cause = { 대기열: 0, 먼차: 0, 교차로: 0 }; for (const r of band) { const tr = r.de / P.ve * fac; if (r.qw >= tr && r.qw >= r.jd) cause.대기열++; else if (tr >= r.jd) cause.먼차++; else cause.교차로++; }
  console.log('  꼬리의 가장 큰 몫: ' + Object.entries(cause).map(([k, v]) => `${k} ${(100 * v / band.length).toFixed(0)}%`).join(' · '));
}
