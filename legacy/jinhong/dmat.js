// dmat — 방향 하나의 스테이션 사이 거리 · 교차로 수 · 흐름별 적재 거리(dl)를 찍는다.  node dmat.js <N> <csv>
const fs = require('fs');
const { loadCase, readSpec } = require('./gx.js');
const [c, f] = process.argv.slice(2), P = loadCase(c), bits = readSpec(f).dir, N = P.nodes.length;
const D = Array.from({ length: N }, (_, i) => { const r = new Array(N).fill(Infinity); r[i] = 0; return r; }), NX = Array.from({ length: N }, (_, i) => { const r = new Array(N).fill(-1); r[i] = i; return r; });
const deg = new Array(N).fill(0); P.edges.forEach(e => { deg[e[0]]++; deg[e[1]]++; });
P.edges.forEach((e, k) => { const a = bits[k] === '1' ? e[1] : e[0], b = bits[k] === '1' ? e[0] : e[1]; D[a][b] = e[2]; NX[a][b] = b; });
for (let k = 0; k < N; k++) for (let i = 0; i < N; i++) { if (D[i][k] === Infinity) continue; for (let j = 0; j < N; j++) { const d = D[i][k] + D[k][j]; if (d < D[i][j]) { D[i][j] = d; NX[i][j] = NX[i][k]; } } }
const nj = (a, b) => { let c2 = a, n = 0; while (c2 !== b) { c2 = NX[c2][b]; if (c2 !== b && deg[c2] >= 3) n++; } return n; };
const nd = P.pd.length, fac = 60 / P.at;
console.log('빈 차 이동 시간(분, 거리/ve + 교차로 통과) 행=차 위치, 열=주문 출발지   [거리m/교차로]');
console.log('        ' + P.depts.map(s => s.padStart(11)).join(''));
for (let x = 0; x < nd; x++) console.log(P.depts[x].padEnd(8) + P.depts.map((_, y) => { const d = D[P.pd[x]][P.pd[y]], j = nj(P.pd[x], P.pd[y]); return `${(d / P.ve * fac + j * P.p * fac).toFixed(2)}[${Math.round(d)}/${j}]`.padStart(11); }).join(''));
const tot = P.flows.reduce((s, x) => s + x[2], 0);
const rows = P.flows.map(([o, d, r]) => ({ o, d, r, dl: D[P.pd[o]][P.pd[d]], u: d === P.urgentDest })).sort((a, b) => a.dl - b.dl);
console.log('흐름(적재 거리 순): ' + rows.map(x => `${x.u ? '★' : ''}${P.depts[x.o]}→${P.depts[x.d]} dl${Math.round(x.dl * 10) / 10} ${(100 * x.r / tot).toFixed(1)}%`).join(' | '));
const inn = new Array(nd).fill(0), out = new Array(nd).fill(0); P.flows.forEach(([o, d, r]) => { out[o] += r; inn[d] += r; });
console.log('도착−출발(빈 차가 쌓이는 곳 +): ' + P.depts.map((s, i) => `${s} ${inn[i] - out[i] > 0 ? '+' : ''}${inn[i] - out[i]}`).join(' '));
