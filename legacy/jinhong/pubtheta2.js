// 공개 점수: (B1 + 기준 2개) 구조를 대수 20 · 21에서 전부 계산한 뒤, 상위 TOP개 구조마다 루트 경계값 0~0.006을 0.00001 간격으로 훑는다.
const fs = require('fs');
const { Engine, readSpec, specToCsv } = require('./cx.js');
const { toTree, toSpec, show, valid, GRID } = require('./race.js');
const clone = t => JSON.parse(JSON.stringify(t)), FE = ['de', 'dl', 'qo', 'urg'];
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
  const TOP = +(process.argv[2] || 100), eng = new Engine(), t0 = Date.now(), el = () => ((Date.now() - t0) / 60000).toFixed(1) + '분';
  const J = t => ({ ...toSpec(t), seed0: 11, count: 5, block: 5, qcut: 120 });
  const evalAll = async list => { for (let i = 0; i < list.length; i += 4000) { const part = list.slice(i, i + 4000), r = await eng.run(part.map(c => J(c.t))); part.forEach((c, j) => { c.pub = r[j] ? r[j][0] : Infinity; }); } };
  const B1 = toTree(readSpec('designs/b1_w0_n21.csv')), m = new Map();
  for (const a of addOne(B1)) for (const t of [a, ...addOne(a)]) if (valid(t)) m.set(JSON.stringify(toSpec(t)), t);
  const l1 = []; for (const n of [20, 21]) for (const t of m.values()) { const c = clone(t); c.n = n; l1.push({ t: c }); }
  await evalAll(l1);
  const top = l1.filter(c => Number.isFinite(c.pub)).sort((a, b) => a.pub - b.pub).slice(0, TOP);
  console.log(`1단계 ${l1.length}개 (${el()}), 상위 ${TOP}개 구조의 공개 점수 ${top[0].pub.toFixed(2)} ~ ${top[TOP - 1].pub.toFixed(2)}`);
  const TH = Array.from({ length: 601 }, (_, i) => +(i * 0.00001).toFixed(5)), l2 = [];
  for (const c of top) for (const th of TH) { const t = clone(c.t); t.root.th = th; l2.push({ t }); }
  await evalAll(l2);
  const seen = new Set(), best = l2.filter(c => Number.isFinite(c.pub)).sort((a, b) => a.pub - b.pub).filter(c => !seen.has(c.pub.toFixed(6)) && seen.add(c.pub.toFixed(6)));
  console.log(`2단계 ${l2.length}개 (${el()}), 서로 다른 값 ${best.length}개. 최저 10개:`);
  best.slice(0, 10).forEach((c, i) => { console.log(`  ${c.pub.toFixed(2)} | n${c.t.n} ${show(c.t.root)}`); fs.writeFileSync(`designs/pubtheta2_${i + 1}.csv`, specToCsv(toSpec(c.t))); });
  eng.close();
})();
