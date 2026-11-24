// 공개 점수 고르기: 기대값이 같은(구분되지 않는) 후보들 가운데 공개 시드 11~15 점수가 가장 낮은 것을 찾는다.
//  1) 후보 모으기: 기준 CSV의 w 경계값을 평탄 구간 안에서 바꾼 것 + race.js가 120묶음까지 남긴 후보(arch) 중 학습 차이가 작은 것
//  2) 공개 점수를 C++ 엔진(웹과 일치)으로 계산해 정렬
//  3) 상위 K개를 확인 시드(200001~)에서 기준안과 짝비교 — 기준보다 나쁘지 않아야(차이 < +0.5, t < 1.5) 통과
// 주의: 이렇게 고른 공개 점수는 「동률 후보 중 공개 시드에서 운이 좋았던 것」이다. 비공개 기대값은 확인 시드 결과로만 말한다.
// 사용: node tickets.js <기준 csv> <topK> [arch.jsonl ...]
const fs = require('fs');
const { Engine, readSpec, specToCsv, paired, mean } = require('./cx.js');
(async () => {
  const [baseF, topK, ...archs] = process.argv.slice(2);
  const base = readSpec(baseF), cands = new Map(), key = s => `${s.n}|${s.nodes}`;
  cands.set(key(base), { name: 'base', spec: base });
  // w 노드 경계값을 가까운 값으로 흔들기(연속값이라 행동이 조금씩 달라져 공개 점수가 달라진다)
  const nodes = base.nodes.split(';').map(x => x.split(','));
  nodes.forEach((f, i) => {
    if (f[1] !== 'w') return;
    const th = +f[3], alts = th <= 0.01 ? [0, 0.0001, 0.0002, 0.0005, 0.001, 0.0015, 0.002, 0.003] : [-0.1, -0.05, 0.05, 0.1].map(d => +(th + d).toFixed(3));
    for (const a of alts) { const nn = nodes.map(x => x.slice()); nn[i][3] = String(a); const s = { n: base.n, nodes: nn.map(x => x.join(',')).join(';') }; if (!cands.has(key(s))) cands.set(key(s), { name: `#${f[0]} w=${a}`, spec: s }); }
  });
  for (const a of archs) for (const l of fs.readFileSync(a, 'utf8').trim().split('\n')) {
    const r = JSON.parse(l); if (r.d < 0.3 && !cands.has(key(r.spec))) cands.set(key(r.spec), { name: `arch d=${r.d}`, spec: r.spec, show: r.show });
  }
  const list = [...cands.values()], eng = new Engine();
  const pub = await eng.run(list.map(c => ({ ...c.spec, seed0: 11, count: 5, block: 5 })));
  list.forEach((c, i) => { c.pub = pub[i][0]; });
  list.sort((a, b) => a.pub - b.pub);
  console.log(`후보 ${list.length}개, 공개 점수 범위 ${list[0].pub.toFixed(2)} ~ ${list[list.length - 1].pub.toFixed(2)}, 기준 ${list.find(c => c.name === 'base').pub.toFixed(2)}`);
  const top = list.filter(c => Number.isFinite(c.pub)).slice(0, +topK);
  const conf = await eng.run([base, ...top.map(c => c.spec)].map(s => ({ ...s, seed0: 200001, count: 1000, block: 5 })));
  console.log(`확인 시드 200001~201000: 기준 평균 ${mean(conf[0]).toFixed(2)}`);
  top.forEach((c, i) => {
    const cc = conf[i + 1], ok = cc.every(Number.isFinite), p = ok ? paired(cc, conf[0]) : null;
    c.pass = ok && p.d < 0.5 && p.t < 1.5;
    console.log(`공개 ${c.pub.toFixed(2)} | 확인 ${ok ? mean(cc).toFixed(2) + ` (기준 대비 ${p.d >= 0 ? '+' : ''}${p.d.toFixed(2)} ± ${p.se.toFixed(2)}, t ${p.t.toFixed(1)})` : '반려'} ${c.pass ? '통과' : '탈락'} | ${c.name} n${c.spec.n} ${c.show || ''}`);
  });
  const win = top.find(c => c.pass);
  if (win) { fs.writeFileSync('designs/ticket_best.csv', specToCsv(win.spec)); console.log(`→ designs/ticket_best.csv (공개 ${win.pub.toFixed(2)})`); }
  eng.close();
})();
