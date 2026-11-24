// GitHub Actions용 Case 1 공개 점수 탐색(조각 실행). 로컬에서도 돈다: node gha_case1.js 0 1 60
//  후보 = (B1 + 기준 2개 구조 약 9천 개) × 대수 19~22. 이 조각(shard)은 그중 index % nshards == shard 인 것만 맡는다.
//  1) 맡은 후보를 루트 경계값 0.001에서 한 번씩 계산  2) 점수가 낮은 순으로 루트 경계값 0~0.006(0.00001 간격)을 촘촘히 훑음
//  3) 시간이 남으면 최저 후보들에서 변이 탐색. 결과는 gha/out/shard_<k>.json에 몇 분마다 저장한다.
// 사용: node gha_case1.js <shard> <nshards> <분>
const fs = require('fs');
const { Engine, readSpec } = require('./cx.js');
const { toTree, toSpec, show, valid, GRID, mutator, makeRng } = require('./race.js');
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
  const [shard, nsh, minutes] = process.argv.slice(2).map(Number), eng = new Engine(), t0 = Date.now();
  const left = () => minutes - (Date.now() - t0) / 60000, el = () => ((Date.now() - t0) / 60000).toFixed(1) + '분';
  fs.mkdirSync('gha/out', { recursive: true });
  const J = t => ({ ...toSpec(t), seed0: 11, count: 5, block: 5, qcut: 120 });
  const best = new Map(); let evals = 0;
  const note = c => { if (!Number.isFinite(c.pub)) return; const k = c.pub.toFixed(6); if (!best.has(k)) best.set(k, { pub: c.pub, spec: toSpec(c.t), show: show(c.t.root) }); if (best.size > 400) { const keep = [...best.entries()].sort((a, b) => a[1].pub - b[1].pub).slice(0, 200); best.clear(); keep.forEach(([k2, v]) => best.set(k2, v)); } };
  const save = tag => { const top = [...best.values()].sort((a, b) => a.pub - b.pub).slice(0, 100); fs.writeFileSync(`gha/out/shard_${shard}.json`, JSON.stringify({ shard, nsh, evals, minutes: +el().replace('분', ''), tag, top }, null, 1)); console.log(`[${el()}] ${tag} 평가 ${evals} 최저 ${top[0] ? top[0].pub.toFixed(2) : '-'}`); };
  const evalAll = async list => { for (let i = 0; i < list.length && left() > 0; i += 2000) { const part = list.slice(i, i + 2000), r = await eng.run(part.map(c => J(c.t))); part.forEach((c, j) => { c.pub = r[j] ? r[j][0] : Infinity; note(c); }); evals += part.length; } };
  const B1 = toTree(readSpec('designs/b1_w0_n21.csv')), m = new Map();
  for (const a of addOne(B1)) for (const t of [a, ...addOne(a)]) if (valid(t)) m.set(JSON.stringify(toSpec(t)), t);
  const all = []; let idx = 0;
  for (const n of [20, 21, 19, 22]) for (const t of m.values()) { if (idx++ % nsh === shard) { const c = clone(t); c.n = n; all.push({ t: c }); } }
  console.log(`조각 ${shard}/${nsh}: 후보 ${all.length}개, ${minutes}분`);
  await evalAll(all); save('1단계');
  const order = all.filter(c => Number.isFinite(c.pub)).sort((a, b) => a.pub - b.pub);
  const TH = Array.from({ length: 601 }, (_, i) => +(i * 0.00001).toFixed(5));
  let lastSave = Date.now(), done = 0;
  const reserve = Math.min(40, minutes * 0.15); // 끝의 변이 탐색 몫
  for (const c of order) {
    if (left() < reserve) break;
    const list = TH.map(th => { const t = clone(c.t); t.root.th = th; return { t }; });
    await evalAll(list); done++;
    if (Date.now() - lastSave > 180000) { save(`2단계 ${done}/${order.length}`); lastSave = Date.now(); }
  }
  save(`2단계 끝 ${done}/${order.length}`);
  const rnd = makeRng(1000 + shard), mut = mutator(rnd, { fixroot: 1, oldonly: 0, nmin: 19, nmax: 22 }), seen = new Set();
  while (left() > 1) {
    const elite = [...best.values()].sort((a, b) => a.pub - b.pub).slice(0, 8).map(e => toTree(e.spec)), pool = [];
    for (let tries = 0; pool.length < 300 && tries < 6000; tries++) {
      const t = mut(elite[Math.floor(rnd() * elite.length)]); if (!t || !valid(t)) continue;
      if (rnd() < 0.5 && t.root.f === 'w') t.root.th = +(Math.floor(rnd() * 600) * 0.00001).toFixed(5);
      const k = JSON.stringify(toSpec(t)); if (seen.has(k)) continue; seen.add(k); pool.push({ t });
    }
    if (!pool.length) break;
    await evalAll(pool);
    if (Date.now() - lastSave > 180000) { save('3단계'); lastSave = Date.now(); }
  }
  save('끝'); eng.close();
})();
