// diag3 — 긴급 대기 분포를 (출발지, 빈 차 거리)별로 센다. 95분위가 어느 묶음에서 정해지는지 본다.  node diag3.js <케이스> <csv>
const fs = require('fs'), path = require('path');
const { loadCase } = require('./gx.js');
const W = require(path.join(__dirname, 'web', 'engine_final.js'));
const [c, f] = process.argv.slice(2), P = loadCase(c);
const ps = W.agvParse_(P, fs.readFileSync(f, 'utf8')), net = W.agvNet_(P, ps.design.dirs), rows = [];
for (const seed of P.seeds) {
  const disp = new Map();
  const s = W.agvSim_(P, net, ps.design, seed, (ev, v, r, x) => {
    if (ev === 'disp') disp.set(r.id, { de: x, t: s.t });
    else if (ev === 'pick' && r.t >= P.warm) { const d = disp.get(r.id); rows.push({ o: r.o, u: r.d === P.urgentDest, w: x, qw: d.t - r.t, de: d.de }); }
  });
  while (s.step()) { }
}
const U = rows.filter(r => r.u).sort((a, b) => a.w - b.w), p95 = U[Math.floor(0.95 * (U.length - 1))].w;
console.log(`긴급 ${U.length}건, p95 ${p95.toFixed(3)}분. 분위: ` + [0.5, 0.8, 0.9, 0.93, 0.94, 0.95, 0.96, 0.97, 0.99].map(p => `${p}:${U[Math.floor(p * (U.length - 1))].w.toFixed(2)}`).join(' '));
const key = r => `${P.depts[r.o]} de${Math.round(r.de)}`, m = new Map();
for (const r of U) { const k = key(r), e = m.get(k) || { n: 0, hi: 0, sum: 0, min: 9, max: 0 }; e.n++; e.sum += r.w; if (r.w >= p95 - 1e-9) e.hi++; e.min = Math.min(e.min, r.w); e.max = Math.max(e.max, r.w); m.set(k, e); }
const top = U.filter(r => r.w >= p95 - 1e-9).length;
console.log(`p95 이상 ${top}건의 구성(묶음: 전체 건수 / p95 이상 건수 / 평균 · 최소 · 최대 대기):`);
[...m.entries()].filter(([, e]) => e.hi > 0 || e.n >= 100).sort((a, b) => b[1].hi - a[1].hi).slice(0, 22).forEach(([k, e]) => console.log(`  ${k.padEnd(12)} ${String(e.n).padStart(4)}건 / ${String(e.hi).padStart(3)}건(${(100 * e.hi / top).toFixed(0)}%) / ${(e.sum / e.n).toFixed(2)} · ${e.min.toFixed(2)} · ${e.max.toFixed(2)}`));
const bins = {}; for (const r of U) if (r.w >= 3.6) { const b = (Math.floor(r.w * 10) / 10).toFixed(1); bins[b] = (bins[b] || 0) + 1; }
console.log('3.6분 이상 0.1분 칸별 건수: ' + Object.entries(bins).sort((a, b) => a[0] - b[0]).map(([b, n]) => `${b}:${n}`).join(' '));
console.log(`배차 전 대기가 있던 긴급: ${U.filter(r => r.qw > 1e-6).length}건, 그중 p95 이상 ${U.filter(r => r.qw > 1e-6 && r.w >= p95).length}건`);
