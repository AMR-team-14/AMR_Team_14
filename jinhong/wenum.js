// wenum — 방금 들어온 주문 쪽(루트 w의 lo 가지)은 그대로 두고, 대기 가지(hi)를 기준 1~3개짜리로 처음부터 전수한다.
//  node wenum.js <케이스> <csv(루트가 w인 답)> [루트 w 경계값들 a,b,c]   → caseN/wenum_<i>.csv (상위 8개)
const fs = require('fs'), path = require('path');
const { Engine, loadCase, readSpec, specToCsv } = require('./gx.js');
const [CN, f, ths0] = process.argv.slice(2), P = loadCase(CN), base = readSpec(f), N = P.nodes.length;
const m = {}; for (const s of base.nodes.split(';')) { const [id, ft, d, th, lo, hi] = s.split(','); m[id] = { ft, d, th: +th, lo: +lo, hi: +hi }; }
if (m[1].ft !== 'w') throw new Error('루트가 w가 아니다');
const D = Array.from({ length: N }, (_, i) => { const r = new Float64Array(N).fill(Infinity); r[i] = 0; return r; });
P.edges.forEach((e, k) => { const a = base.dir[k] === '1' ? e[1] : e[0], b = base.dir[k] === '1' ? e[0] : e[1]; D[a][b] = e[2]; });
for (let k = 0; k < N; k++) for (let i = 0; i < N; i++) for (let j = 0; j < N; j++) if (D[i][k] + D[k][j] < D[i][j]) D[i][j] = D[i][k] + D[k][j];
const sv = new Set([0]); for (const a of P.pd) for (const b of P.pd) sv.add(+D[a][b].toFixed(4));
const DE = [...sv].sort((a, b) => a - b), DL = [...new Set(P.flows.map(x => +D[P.pd[x[0]]][P.pd[x[1]]].toFixed(4)))].sort((a, b) => a - b).slice(0, -1);
const atoms = [];   // [특성, 방향, 경계값]
for (const d of ['min', 'max']) { atoms.push(['urg', d, 0.5]); for (const v of DE) atoms.push(['de', d, v]); for (const v of DL) atoms.push(['dl', d, v]); for (const v of [0, 1, 2, 3, 4, 5]) atoms.push(['qo', d, v]); }
// 방금 들어온 주문 쪽 가지를 그대로 복사(id 다시 매김)
function freshNodes(next) { const out = []; const rec = id => { if (!id) return 0; const nd = m[id], my = next.v++; const lo = rec(nd.lo), hi = rec(nd.hi); out.push(`${my},${nd.ft},${nd.d},${nd.th},${lo},${hi}`); return my; }; const root = rec(m[1].lo); return { root, out }; }
function spec(th, W) {   // W = [atom] | [atom, side, atom] | [atom, atomLo, atomHi](side = 'both')
  const next = { v: 2 }, fr = freshNodes(next), out = [...fr.out]; let wid = 0;
  if (W) { wid = next.v++; const [a, s, b, c] = W; let lo = 0, hi = 0; if (s === 'lo') { lo = next.v++; out.push(`${lo},${b[0]},${b[1]},${b[2]},0,0`); } else if (s === 'hi') { hi = next.v++; out.push(`${hi},${b[0]},${b[1]},${b[2]},0,0`); } else if (s === 'both') { lo = next.v++; out.push(`${lo},${b[0]},${b[1]},${b[2]},0,0`); hi = next.v++; out.push(`${hi},${c[0]},${c[1]},${c[2]},0,0`); } out.push(`${wid},${a[0]},${a[1]},${a[2]},${lo},${hi}`); }
  return { dir: base.dir, n: base.n, nodes: [`1,w,${m[1].d},${th},${fr.root},${wid}`, ...out].join(';') };
}
const show = W => !W ? '-' : `${W[0][0]}(${W[0][1]},${W[0][2]})` + (W[1] ? (W[1] === 'both' ? `[${W[2][0]}(${W[2][1]},${W[2][2]}),${W[3][0]}(${W[3][1]},${W[3][2]})]` : W[1] === 'lo' ? `[${W[2][0]}(${W[2][1]},${W[2][2]}),-]` : `[-,${W[2][0]}(${W[2][1]},${W[2][2]})]`) : '');
(async () => {
  const eng = new Engine(CN, +(process.env.AGVX_THREADS || 14)), t0 = Date.now(), ths = ths0 ? ths0.split(',').map(Number) : [m[1].th], el = () => ((Date.now() - t0) / 60000).toFixed(1);
  const Ws = [null]; for (const a of atoms) Ws.push([a]);
  for (const a of atoms) for (const b of atoms) if (b[0] !== a[0]) { Ws.push([a, 'lo', b]); Ws.push([a, 'hi', b]); }
  const roots3 = atoms.filter(a => a[0] === 'urg' || (a[0] === 'de' && [65, 85, 110, 150].includes(a[2])) || (a[0] === 'qo' && a[2] === 1));
  for (const a of roots3) for (const b of atoms) if (b[0] !== a[0]) for (const c of atoms) if (c[0] !== a[0]) Ws.push([a, 'both', b, c]);
  console.log(`대기 가지 후보 ${Ws.length}개 × 루트 경계값 ${ths.length}개 (기본 조각 ${atoms.length}개)`);
  const all = [];
  for (const th of ths) for (let i = 0; i < Ws.length; i += 20000) { const part = Ws.slice(i, i + 20000), r = await eng.pub(part.map(W => spec(th, W))); part.forEach((W, k) => all.push({ th, W, pub: r[k] })); console.log(`  ${th} ${Math.min(i + 20000, Ws.length)}/${Ws.length} [${el()}분] 지금까지 최저 ${Math.min(...all.map(x => x.pub)).toFixed(3)}`); }
  all.sort((a, b) => a.pub - b.pub);
  const seen = new Set(), top = all.filter(x => !seen.has(x.pub.toFixed(6)) && seen.add(x.pub.toFixed(6))).slice(0, 12);
  top.forEach((x, i) => { console.log(`  ${i + 1}. ${x.pub.toFixed(3)} | 루트 w ${x.th} | 대기 가지 ${show(x.W)}`); if (i < 8) fs.writeFileSync(path.join(__dirname, 'case' + CN, `wenum_${i + 1}.csv`), specToCsv(spec(x.th, x.W))); });
  console.log(`[끝] ${el()}분, 평가 ${all.length}`); eng.close();
})();
