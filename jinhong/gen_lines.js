// gen_lines — 격자 지도(Case 4)의 방향 후보를 「줄 단위」로 만든다.
//  ① 줄 균일 방향 전수: 가로 6줄 · 세로 6줄마다 한 방향(2^12) × 대각 통로 6개(2^6) 중 강연결인 것 → 정적 점수(적재 + 빈 차 + 긴급 가중, 교차로 통과를 거리로 환산)로 상위만 남긴다.
//  ② 큰 걸음 이웃: 기준 답의 방향에서 줄 하나 · 줄 둘 · 줄 하나 + 대각 하나를 통째로 뒤집은 방향.
//  node gen_lines.js <N> <기준 csv> [정적 상위 몇 개=1500] [교차로 환산 m=25]
//   → caseN/dirs_lines.json (①), caseN/dirs_macro.json (②)
const fs = require('fs'), path = require('path');
const { loadCase, strong, readSpec } = require('./gx.js');
const [CN, baseF, TOP = '1500', XM = '25'] = process.argv.slice(2), P = loadCase(CN), OUT = path.join(__dirname, 'case' + CN);
const N = P.nodes.length, E = P.edges.length, X = P.nodes.map(n => n[1]), Y = P.nodes.map(n => n[2]);
const deg = new Array(N).fill(0); P.edges.forEach(e => { deg[e[0]]++; deg[e[1]]++; });
// 줄: 같은 y(가로) 또는 같은 x(세로)인 통로. 나머지는 대각.
const lines = new Map(), diag = [];
P.edges.forEach((e, k) => { const [a, b] = e; if (Y[a] === Y[b]) { const key = 'h' + Y[a]; if (!lines.has(key)) lines.set(key, []); lines.get(key).push(k); } else if (X[a] === X[b]) { const key = 'v' + X[a]; if (!lines.has(key)) lines.set(key, []); lines.get(key).push(k); } else diag.push(k); });
const LN = [...lines.entries()].map(([key, ks]) => ({ key, ks }));
// 줄의 「정방향」(x 또는 y가 커지는 쪽)으로 가는 비트
const fwdBit = k => { const [a, b] = P.edges[k]; return (X[a] + Y[a] < X[b] + Y[b]) ? '0' : '1'; };
const flipc = c => c === '1' ? '0' : '1';
console.log(`줄 ${LN.length}개(${LN.map(l => l.key + ':' + l.ks.length).join(' ')}), 대각 ${diag.length}개`);
// 정적 점수
const D = new Float64Array(N * N), xm = +XM;
const arr = new Array(P.pd.length).fill(0), dep = new Array(P.pd.length).fill(0); let tot = 0;
for (const [o, d, r] of P.flows) { dep[o] += r; arr[d] += r; tot += r; }
const urgO = new Array(P.pd.length).fill(0); for (const [o, d, r] of P.flows) if (d === P.urgentDest) urgO[o] += r;
function stat(bits) {
  D.fill(Infinity); for (let i = 0; i < N; i++) D[i * N + i] = 0;
  for (let k = 0; k < E; k++) { const e = P.edges[k], a = bits[k] === '1' ? e[1] : e[0], b = bits[k] === '1' ? e[0] : e[1]; D[a * N + b] = e[2] + (deg[b] >= 3 ? xm : 0); }
  for (let k = 0; k < N; k++) for (let i = 0; i < N; i++) { const dik = D[i * N + k]; if (dik === Infinity) continue; const ik = i * N, kk = k * N; for (let j = 0; j < N; j++) { const d = dik + D[kk + j]; if (d < D[ik + j]) D[ik + j] = d; } }
  const T = (a, b) => D[P.pd[a] * N + P.pd[b]];
  let L = 0, Em = 0, U = 0;
  for (const [o, d, r] of P.flows) L += r * T(o, d);
  for (let o = 0; o < P.pd.length; o++) for (let d = 0; d < P.pd.length; d++) { if (o === d) continue; const w = arr[d] / tot * T(d, o); Em += dep[o] * w; U += urgO[o] * w; }
  return { L, Em, U };
}
// ① 줄 균일 전수
const K = LN.length, res = [];
if (K <= 14 && diag.length <= 8) {
  const b = new Array(E);
  for (let m = 0; m < (1 << K); m++) {
    for (let c = 0; c < K; c++) for (const k of LN[c].ks) b[k] = (m >> c & 1) ? flipc(fwdBit(k)) : fwdBit(k);
    for (let dm = 0; dm < (1 << diag.length); dm++) {
      diag.forEach((k, i) => { b[k] = (dm >> i & 1) ? '1' : '0'; });
      const bits = b.join(''); if (!strong(P, bits)) continue;
      res.push({ dir: bits, ...stat(bits) });
    }
  }
  const pick = new Map(), add = (list, tag) => list.slice(0, +TOP).forEach(r => { if (!pick.has(r.dir)) pick.set(r.dir, { ...r, by: tag }); });
  add([...res].sort((a, b2) => (a.L + a.Em) - (b2.L + b2.Em)), 'L+E');
  add([...res].sort((a, b2) => (a.L + a.Em + 3 * a.U) - (b2.L + b2.Em + 3 * b2.U)), 'L+E+3U');
  add([...res].sort((a, b2) => (0.5 * a.L + a.Em + 3 * a.U) - (0.5 * b2.L + b2.Em + 3 * b2.U)), '0.5L+E+3U');
  fs.writeFileSync(path.join(OUT, 'dirs_lines.json'), JSON.stringify({ cands: [...pick.values()] }));
  console.log(`① 줄 균일 강연결 ${res.length}개 → 정적 상위 ${pick.size}개 (dirs_lines.json)`);
}
// ② 큰 걸음 이웃
if (baseF) {
  const base = readSpec(baseF).dir, s0 = stat(base), set = new Set();
  const fl = (bits, ks) => { const b = bits.split(''); for (const k of ks) b[k] = flipc(b[k]); return b.join(''); };
  const add = d => { if (d !== base && strong(P, d)) set.add(d); };
  // 줄을 통째로 뒤집기 / 줄을 정방향 · 역방향으로 균일하게 맞추기
  const uni = (bits, c, rev) => { const b = bits.split(''); for (const k of LN[c].ks) b[k] = rev ? flipc(fwdBit(k)) : fwdBit(k); return b.join(''); };
  const moves = [];
  for (let c = 0; c < K; c++) { moves.push(d => fl(d, LN[c].ks)); moves.push(d => uni(d, c, false)); moves.push(d => uni(d, c, true)); }
  for (const k of diag) moves.push(d => fl(d, [k]));
  for (const m1 of moves) { const d1 = m1(base); add(d1); for (const m2 of moves) add(m2(d1)); }
  fs.writeFileSync(path.join(OUT, 'dirs_macro.json'), JSON.stringify({ cands: [...set].map(dir => ({ dir })) }));
  console.log(`② 기준 방향 정적 L ${s0.L.toFixed(0)} Em ${s0.Em.toFixed(0)} U ${s0.U.toFixed(0)} | 큰 걸음 이웃 ${set.size}개 (dirs_macro.json)`);
  if (res.length) { const q = (f, v) => res.filter(r => f(r) <= v).length; console.log(`  기준 방향의 정적 순위(줄 균일 ${res.length}개 중): L+E ${q(r => r.L + r.Em, s0.L + s0.Em) + 1}등, L+E+3U ${q(r => r.L + r.Em + 3 * r.U, s0.L + s0.Em + 3 * s0.U) + 1}등`); }
}
