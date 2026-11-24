// joint4 — 방향 고정, urg(max)[일반 dl(tn)[a,b], 긴급 dl(tu)[c,d]]를 일반 쪽 후보 × 긴급 쪽 전수로 함께 훑는다(s4는 한쪽씩만 훑는다).
//  node joint4.js <케이스> <csv> <대수들> <반경 상한>
const fs = require('fs'), path = require('path');
const { Engine, loadCase, readSpec, specToCsv } = require('./gx.js');
const [CN, f, ns0, cap = '160'] = process.argv.slice(2), P = loadCase(CN), base = readSpec(f), N = P.nodes.length;
const D = Array.from({ length: N }, (_, i) => { const r = new Float64Array(N).fill(Infinity); r[i] = 0; return r; });
P.edges.forEach((e, k) => { const a = base.dir[k] === '1' ? e[1] : e[0], b = base.dir[k] === '1' ? e[0] : e[1]; D[a][b] = e[2]; });
for (let k = 0; k < N; k++) for (let i = 0; i < N; i++) for (let j = 0; j < N; j++) if (D[i][k] + D[k][j] < D[i][j]) D[i][j] = D[i][k] + D[k][j];
const sv = new Set([0]); for (const a of P.pd) for (const b of P.pd) sv.add(+D[a][b].toFixed(4));
const R = [-1, ...[...sv].filter(v => v <= +cap).sort((a, b) => a - b)];
const dls = u => [...new Set(P.flows.filter(x => (x[1] === P.urgentDest) === u).map(x => +D[P.pd[x[0]]][P.pd[x[1]]].toFixed(4)))].sort((a, b) => a - b);
const dn = dls(false), du = dls(true);
function nodes(S) {   // S = {tn,a,b,tu,c,d}; t === null → 묶음 하나(반경 a 또는 c)
  const out = []; let next = 2;
  const side = (t, x, y) => { if (t === null) { if (x < 0) return 0; const id = next++; out.push(`${id},de,max,${x},0,0`); return id; } if (x < 0 && y < 0) return 0; const id = next++, lo = x < 0 ? 0 : next++, hi = y < 0 ? 0 : next++; out.push(`${id},dl,min,${t},${lo},${hi}`); if (lo) out.push(`${lo},de,max,${x},0,0`); if (hi) out.push(`${hi},de,max,${y},0,0`); return id; };
  const l = side(S.tn, S.a, S.b), h = side(S.tu, S.c, S.d);
  return [`1,urg,max,0.5,${l},${h}`, ...out].join(';');
}
(async () => {
  const eng = new Engine(CN, +(process.env.AGVX_THREADS || 14)), t0 = Date.now(), ns = ns0.split(',').map(Number);
  const NS = []; for (const tn of dn.slice(-5, -1)) for (const b of R.filter(r => r >= 45 && r <= 126.3)) NS.push({ tn, a: -1, b });
  const US = [{ tu: null, c: -1, d: -1 }]; for (const tu of du.slice(0, -1)) for (const c of R) for (const d of R) if (c >= 0 || d >= 0) US.push({ tu, c, d });
  for (const c of R) if (c >= 0) US.push({ tu: null, c, d: -1 });
  console.log(`일반 후보 ${NS.length} × 긴급 후보 ${US.length} × 대수 ${ns.length} = ${NS.length * US.length * ns.length}건 (일반 dl ${dn.join(',')} | 긴급 dl ${du.join(',')} | 반경 ${R.length}개)`);
  const all = [];
  for (const n of ns) for (const a of NS) { const list = US.map(u => ({ ...a, ...u })), r = await eng.pub(list.map(S => ({ dir: base.dir, n, nodes: nodes(S) }))); list.forEach((S, i) => all.push({ S, n, pub: r[i] })); }
  all.sort((x, y) => x.pub - y.pub);
  const show = S => `일반 dl>${S.tn}: ${S.a < 0 ? '-' : '≤는 ' + S.a} / ${S.b < 0 ? '-' : S.b + 'm 넘는 차'} | 긴급 ${S.tu === null ? (S.c < 0 ? '가까운 차' : '전부 ' + S.c + 'm 넘는 차') : `dl≤${S.tu}: ${S.c < 0 ? '-' : S.c} / dl>${S.tu}: ${S.d < 0 ? '-' : S.d}`}`;
  all.slice(0, 12).forEach((x, i) => console.log(`  ${i + 1}. ${x.pub.toFixed(3)} n${x.n} | ${show(x.S)}`));
  const b0 = all.filter(x => x.S.tu === null && x.S.c < 0)[0]; console.log(`  긴급 가까운 차 고정 최저: ${b0.pub.toFixed(3)} n${b0.n} | ${show(b0.S)}`);
  fs.writeFileSync(path.join(__dirname, 'case' + CN, 'joint4_best.csv'), specToCsv({ dir: base.dir, n: all[0].n, nodes: nodes(all[0].S) }));
  console.log(`[끝] ${((Date.now() - t0) / 60000).toFixed(1)}분 → joint4_best.csv`); eng.close();
})();
