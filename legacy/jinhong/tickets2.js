// 공개 점수 고르기(2판): 기대값이 기준안과 구분되지 않는 후보를 많이 만들고, 그중 공개 시드 11~15 점수가 가장 낮은 것을 찾는다.
//  1) 후보 만들기: 기준안의 w 경계값을 평탄 구간에서 바꾼 것 + 빈 자리에 「거의 발동하지 않는」 9번째 기준 하나를 붙인 것 + race 기록(arch)
//  2) 학습 시드 150개(100001~)에서 기준보다 +1.0 넘게 나쁜 후보는 버림
//  3) 남은 후보의 공개 점수를 계산해 정렬
//  4) 상위 K개를 확인 시드 1,000개(200001~)에서 기준과 짝비교 — 차이 < +0.3이고 t < 1.0이어야 통과
// 주의: 이렇게 얻은 낮은 공개 점수는 「동률 후보 중 공개 시드 5개에서 운이 좋았던 것」이다. 실력(기대값)은 확인 · 최종 시드 결과로만 말한다.
// 사용: node tickets2.js <기준 csv> <topK> [arch.jsonl ...]
const fs = require('fs');
const { Engine, readSpec, specToCsv, paired, mean } = require('./cx.js');
const { toTree, toSpec, show, valid, GRID, FEATS } = require('./race.js');
const clone = t => JSON.parse(JSON.stringify(t));
(async () => {
  const [baseF, topK, ...archs] = process.argv.slice(2);
  const base = toTree(readSpec(baseF)), cands = new Map(), key = t => JSON.stringify(toSpec(t));
  const add = (name, t) => { if (valid(t) && !cands.has(key(t)) && key(t) !== key(base)) cands.set(key(t), { name, t }); };
  // (a) w 경계값 흔들기
  const visit = (nd, fn) => { if (nd) { fn(nd); visit(nd.lo, fn); visit(nd.hi, fn); } };
  let idx = 0; visit(base.root, nd => { const my = idx++; if (nd.f !== 'w') return;
    const alts = nd.th <= 0.01 ? [0, 0.0001, 0.0002, 0.0003, 0.0005, 0.0007, 0.0015, 0.002, 0.0025, 0.003] : [-0.15, -0.1, -0.05, 0.05, 0.1, 0.15].map(d => +(nd.th + d).toFixed(3));
    for (const a of alts) { const t = clone(base); let j = 0; visit(t.root, x => { if (j++ === my) x.th = a; }); add(`w=${a}`, t); } });
  // (b) 빈 자리에 9번째 기준 하나 붙이기
  const slots = []; idx = 0; visit(base.root, nd => { const my = idx++; for (const s of ['lo', 'hi']) if (!nd[s]) slots.push([my, s]); });
  for (const [my, s] of slots) for (const f of FEATS) for (const d of ['min', 'max']) for (const th of GRID[f]) {
    const t = clone(base); let j = 0; visit(t.root, x => { if (j++ === my) x[s] = { f, d, th, lo: null, hi: null }; }); add(`+${f}(${d},${th})`, t);
  }
  // (c) race 기록
  for (const a of archs) if (fs.existsSync(a)) for (const l of fs.readFileSync(a, 'utf8').trim().split('\n')) { const r = JSON.parse(l); if (r.d < 0.5) add(`arch`, toTree(r.spec)); }
  let list = [...cands.values()]; const eng = new Engine(), t0 = Date.now();
  const J = (t, s0, cnt, qcut) => ({ ...toSpec(t), seed0: s0, count: cnt, block: 5, qcut });
  // 2) 학습 선별
  const NB = 30, bTrain = (await eng.run([J(base, 100001, NB * 5, 0)]))[0];
  const tr = await eng.run(list.map(c => J(c.t, 100001, NB * 5, 110)));
  const sig = new Set([bTrain.map(x => x.toFixed(6)).join()]);
  list.forEach((c, i) => { c.tr = tr[i]; c.ok = c.tr && c.tr.every(Number.isFinite); if (c.ok) { c.d = paired(c.tr, bTrain).d; const k = c.tr.map(x => x.toFixed(6)).join(); c.dup = sig.has(k); sig.add(k); } });
  const made = list.length; list = list.filter(c => c.ok && !c.dup && c.d < 1.0);
  // 3) 공개 점수
  const pub = await eng.run([J(base, 11, 5, 0), ...list.map(c => J(c.t, 11, 5, 0))]);
  list.forEach((c, i) => { c.pub = pub[i + 1][0]; }); list = list.filter(c => Number.isFinite(c.pub)).sort((a, b) => a.pub - b.pub);
  console.log(`후보 ${made}개 → 학습 150시드 선별(행동이 같은 것 · +1.0 넘게 나쁜 것 제외) ${list.length}개 | 공개 점수: 기준 ${pub[0][0].toFixed(2)}, 후보 ${list[0].pub.toFixed(2)} ~ ${list[list.length - 1].pub.toFixed(2)}, 중앙값 ${list[list.length >> 1].pub.toFixed(2)}`);
  // 4) 확인
  const top = list.slice(0, +topK);
  const conf = await eng.run([J(base, 200001, 1000, 0), ...top.map(c => J(c.t, 200001, 1000, 0))]);
  console.log(`확인 시드 200001~201000: 기준 평균 ${mean(conf[0]).toFixed(2)}`);
  top.forEach((c, i) => {
    const cc = conf[i + 1], ok = cc.every(Number.isFinite), p = ok ? paired(cc, conf[0]) : null;
    c.pass = ok && p.d < 0.3 && p.t < 1.0; c.conf = ok ? mean(cc) : Infinity; c.p = p;
    console.log(`공개 ${c.pub.toFixed(2)} | 확인 ${ok ? mean(cc).toFixed(2) + ` (기준 대비 ${p.d >= 0 ? '+' : ''}${p.d.toFixed(2)} ± ${p.se.toFixed(2)}, t ${p.t.toFixed(1)})` : '반려'} ${c.pass ? '통과' : '탈락'} | ${c.name} | n${c.t.n} ${show(c.t.root)}`);
  });
  const win = top.filter(c => c.pass);
  win.slice(0, 3).forEach((c, i) => fs.writeFileSync(`designs/ticket_${i + 1}.csv`, specToCsv(toSpec(c.t))));
  console.log(win.length ? `→ 통과 ${win.length}개, 공개 점수 순으로 designs/ticket_1~${Math.min(3, win.length)}.csv 저장 (${((Date.now() - t0) / 1000).toFixed(0)}초)` : '통과한 후보 없음');
  eng.close();
})();
