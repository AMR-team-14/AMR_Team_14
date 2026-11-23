// 리더보드 로드맵 S2~S3: 계열을 통째로 훑어 공개 점수(시드 11~15)가 가장 낮은 후보를 찾는다. 기대값 조건 없음.
//  S2: 대수 19~22 × (B2 + 기준 1개) × 루트 경계값 31개 → 대수별 공개 점수 분포
//  S3: 유리한 대수 2개에서 (B1 + 기준 2개) 전부 → 하위 600개에 루트 경계값 61개
const fs = require('fs');
const { Engine, readSpec, specToCsv } = require('./cx.js');
const { toTree, toSpec, show, valid, GRID } = require('./race.js');
const clone = t => JSON.parse(JSON.stringify(t));
const FE = ['de', 'dl', 'qo', 'urg'];
function addOne(t) {
  const out = [], paths = [];
  (function walk(nd, anc, path) { if (!nd) return; for (const s of ['lo', 'hi']) { if (!nd[s]) paths.push([path.concat(s), anc.concat(nd.f)]); else walk(nd[s], anc.concat(nd.f), path.concat(s)); } })(t.root, [], []);
  for (const [path, anc] of paths) for (const f of FE) { if (anc.includes(f)) continue; for (const d of ['min', 'max']) for (const th of GRID[f]) {
    const c = clone(t); let nd = c.root; for (let i = 0; i < path.length - 1; i++) nd = nd[path[i]];
    nd[path[path.length - 1]] = { f, d, th, lo: null, hi: null }; out.push(c);
  } }
  return out;
}
(async () => {
  const eng = new Engine(), t0 = Date.now(), el = () => ((Date.now() - t0) / 60000).toFixed(1) + '분';
  const B1 = toTree(readSpec('designs/b1_w0_n21.csv')), B2 = toTree(readSpec('designs/b2_n21.csv'));
  const J = t => ({ ...toSpec(t), seed0: 11, count: 5, block: 5, qcut: 120 });
  const evalAll = async list => { for (let i = 0; i < list.length; i += 4000) { const part = list.slice(i, i + 4000), r = await eng.run(part.map(c => J(c.t))); part.forEach((c, j) => { c.pub = r[j] ? r[j][0] : Infinity; }); } };
  const uniq = ts => { const m = new Map(); for (const t of ts) if (valid(t)) m.set(JSON.stringify(toSpec(t)), t); return [...m.values()]; };
  const withN = (t, n, th) => { const c = clone(t); c.n = n; if (th !== undefined) c.root.th = th; return c; };
  const out = [], log = s => { console.log(s); out.push(s); fs.writeFileSync('runs/exp25_pubmap.txt', out.join('\n') + '\n'); };
  // S2
  const A = uniq([B2, ...addOne(B2)]), TH31 = Array.from({ length: 31 }, (_, i) => +(i * 0.0001).toFixed(4)), stat = {};
  let all = [];
  for (const n of [19, 20, 21, 22]) {
    const list = []; for (const t of A) for (const th of TH31) list.push({ t: withN(t, n, th), n });
    await evalAll(list);
    const v = [...new Set(list.filter(c => Number.isFinite(c.pub)).map(c => c.pub.toFixed(6)))].map(Number).sort((a, b) => a - b);
    stat[n] = v[Math.floor(v.length * 0.05)];
    log(`S2 ${n}대: 서로 다른 값 ${v.length}개, 최저 ${v[0].toFixed(2)}, 5% ${stat[n].toFixed(2)}, 중앙값 ${v[v.length >> 1].toFixed(2)} (${el()})`);
    all = all.concat(list);
  }
  const ns = Object.keys(stat).map(Number).sort((a, b) => stat[a] - stat[b]).slice(0, 2);
  log(`→ 유리한 대수: ${ns.join(', ')}`);
  // S3
  const B = uniq(addOne(B1).flatMap(a => [a, ...addOne(a)]));
  const l3 = []; for (const n of ns) for (const t of B) l3.push({ t: withN(t, n, 0.001), n });
  await evalAll(l3);
  const fin3 = l3.filter(c => Number.isFinite(c.pub)).sort((a, b) => a.pub - b.pub);
  log(`S3-1: 구조 ${B.length}개 × 대수 ${ns.length} = ${l3.length}개, 최저 ${fin3[0].pub.toFixed(2)} (${el()})`);
  const TH61 = Array.from({ length: 61 }, (_, i) => +(i * 0.00005).toFixed(5)), l4 = [];
  for (const c of fin3.slice(0, 600)) for (const th of TH61) l4.push({ t: withN(c.t, c.n, th), n: c.n });
  await evalAll(l4);
  all = all.concat(l3, l4);
  const seen = new Set(), best = all.filter(c => Number.isFinite(c.pub)).sort((a, b) => a.pub - b.pub).filter(c => !seen.has(c.pub.toFixed(6)) && seen.add(c.pub.toFixed(6))).slice(0, 12);
  log(`S3-2: 하위 600개 × 루트 경계값 61개 = ${l4.length}개 (${el()}). 전체 ${all.length}개 중 최저 12개:`);
  best.forEach((c, i) => { log(`  ${c.pub.toFixed(2)} | n${c.t.n} ${show(c.t.root)}`); fs.writeFileSync(`designs/pubmap_${i + 1}.csv`, specToCsv(toSpec(c.t))); });
  eng.close();
})();
