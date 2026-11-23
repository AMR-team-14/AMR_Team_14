// 공개 점수 사냥(3판, 자문자답 6회전): 후보 풀을 수만 개로 넓힌다.
//  풀: B1(7노드)의 빈 자리에 기준 2개를 붙인 모든 나무(9노드) + B2에 1개 붙인 나무. 대수 21대, 루트 w 0.001.
//  1) 전부 공개 시드 11~15 점수를 계산한다(후보당 5시드).
//  2) 공개 점수 하위 TOPN개만 기대값을 본다: 학습 150시드(+0.6 안쪽) → 600시드(+0.3 안쪽), 기준은 B2.
//  3) 남은 후보마다 루트 w 경계값 0~0.003을 훑어 공개 점수가 가장 낮은 값을 고른다.
//  4) 확인 시드 1,000개(200001~)에서 B2와 짝비교: 차이 < +0.3, t < 1.0이어야 통과.
// 사용: node pubhunt3.js [TOPN=400]
const fs = require('fs');
const { Engine, readSpec, specToCsv, paired, mean } = require('./cx.js');
const { toTree, toSpec, show, valid, GRID } = require('./race.js');
const clone = t => JSON.parse(JSON.stringify(t));
const FE = ['de', 'dl', 'qo', 'urg'];
function addOne(t) { // 빈 자리마다 가능한 기준을 하나 붙인 나무 전부
  const out = [], paths = [];
  (function walk(nd, anc, path) { if (!nd) return; for (const s of ['lo', 'hi']) { if (!nd[s]) paths.push([path.concat(s), anc.concat(nd.f)]); else walk(nd[s], anc.concat(nd.f), path.concat(s)); } })(t.root, [], []);
  for (const [path, anc] of paths) for (const f of FE) { if (anc.includes(f)) continue; for (const d of ['min', 'max']) for (const th of GRID[f]) {
    const c = clone(t); let nd = c.root; for (let i = 0; i < path.length - 1; i++) nd = nd[path[i]];
    nd[path[path.length - 1]] = { f, d, th, lo: null, hi: null }; out.push(c);
  } }
  return out;
}
(async () => {
  const TOPN = +(process.argv[2] || 400), eng = new Engine(), t0 = Date.now(), el = () => ((Date.now() - t0) / 60000).toFixed(1) + '분';
  const B1 = toTree(readSpec('designs/b1_w0_n21.csv')), B2 = toTree(readSpec('designs/b2_n21.csv'));
  const pool = new Map(), key = t => JSON.stringify(toSpec(t)), put = t => { if (valid(t)) { const k = key(t); if (!pool.has(k)) pool.set(k, { t }); } };
  for (const a of addOne(B1)) { put(a); for (const b of addOne(a)) put(b); }
  for (const a of addOne(B2)) put(a);
  let list = [...pool.values()];
  const J = (t, s0, cnt, qcut) => ({ ...toSpec(t), seed0: s0, count: cnt, block: 5, qcut });
  console.log(`후보 ${list.length}개 생성`);
  // 1) 공개 점수 (5만 개면 나눠서)
  for (let i = 0; i < list.length; i += 4000) { const part = list.slice(i, i + 4000), r = await eng.run(part.map(c => J(c.t, 11, 5, 120))); part.forEach((c, j) => { c.pub = r[j] ? r[j][0] : Infinity; }); }
  const sig = new Set(); list = list.filter(c => Number.isFinite(c.pub) && !sig.has(c.pub.toFixed(8)) && sig.add(c.pub.toFixed(8))).sort((a, b) => a.pub - b.pub);
  const q = p => list[Math.floor(list.length * p)].pub.toFixed(2);
  console.log(`공개 점수 계산 끝(${el()}): 서로 다른 값 ${list.length}개, 최저 ${list[0].pub.toFixed(2)}, 1% ${q(0.01)}, 10% ${q(0.1)}, 중앙값 ${q(0.5)}`);
  // 2) 기대값 선별
  let top = list.slice(0, TOPN);
  const bTr = (await eng.run([J(B2, 100001, 600, 0)]))[0];
  for (const [nb, thr] of [[30, 0.6], [120, 0.3]]) {
    const r = await eng.run(top.map(c => J(c.t, 100001, nb * 5, 120)));
    top.forEach((c, i) => { c.ok = r[i] && r[i].every(Number.isFinite); if (c.ok) c.trd = paired(r[i], bTr.slice(0, nb)).d; });
    top = top.filter(c => c.ok && c.trd < thr);
    console.log(`  공개 하위 ${TOPN}개 중 학습 ${nb * 5}시드에서 B2 대비 < +${thr}: ${top.length}개 (${el()})`);
  }
  // 3) 루트 경계값 훑기
  const TH = Array.from({ length: 61 }, (_, i) => +(i * 0.00005).toFixed(5)), cand = [];
  for (const c of top) for (const th of TH) { const t = clone(c.t); t.root.th = th; cand.push({ t, parent: c }); }
  for (let i = 0; i < cand.length; i += 4000) { const part = cand.slice(i, i + 4000), r = await eng.run(part.map(c => J(c.t, 11, 5, 120))); part.forEach((c, j) => { c.pub = r[j] ? r[j][0] : Infinity; }); }
  const s2 = new Set(); let fin = cand.filter(c => Number.isFinite(c.pub) && !s2.has(c.pub.toFixed(8)) && s2.add(c.pub.toFixed(8))).sort((a, b) => a.pub - b.pub).slice(0, 30);
  // 4) 확인
  const conf = await eng.run([J(B2, 200001, 1000, 0), ...fin.map(c => J(c.t, 200001, 1000, 0))]);
  console.log(`루트 경계값까지 훑은 뒤 공개 하위 30개를 확인 시드로 (${el()}). B2 확인 평균 ${mean(conf[0]).toFixed(2)}`);
  fin.forEach((c, i) => { const cc = conf[i + 1]; c.cok = cc.every(Number.isFinite); if (c.cok) { c.conf = mean(cc); c.p = paired(cc, conf[0]); } c.pass = c.cok && c.p.d < 0.3 && c.p.t < 1.0; });
  fin.slice(0, 14).forEach(c => console.log(`공개 ${c.pub.toFixed(2)} | 확인 ${c.cok ? `${c.p.d >= 0 ? '+' : ''}${c.p.d.toFixed(2)} ± ${c.p.se.toFixed(2)}` : '반려'} ${c.pass ? '통과' : '탈락'} | ${show(c.t.root)}`));
  const win = fin.filter(c => c.pass);
  win.slice(0, 5).forEach((c, i) => fs.writeFileSync(`designs/hunt3_${i + 1}.csv`, specToCsv(toSpec(c.t))));
  console.log(win.length ? `→ 통과 ${win.length}개. 최저 공개 ${win[0].pub.toFixed(2)} → designs/hunt3_1.csv (${el()})` : `통과 없음 (${el()})`);
  eng.close();
})();
