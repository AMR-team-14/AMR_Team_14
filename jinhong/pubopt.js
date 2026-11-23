// 공개 점수 직접 최적화(자문자답 7회전, 진홍 결정: 리더보드 점수가 낮은 것이 목표).
// 목적함수 = 공개 시드 11~15의 점수 그 자체다. 기대값 조건을 두지 않으므로 공개 시드 5개에 맞춰진다(과적합).
//  - 한 세대에 후보 LAMBDA개: 루트 w 경계값만 바꾼 것 / 1~4곳 바꾼 것 / 대수 ±1
//  - 상위 8개(엘리트)에서 부모를 뽑고, 가장 낮은 점수가 나오면 갱신한다.
// 사용: node pubopt.js <시작 csv> <분> <출력 접두어> [난수 씨앗]
const fs = require('fs');
const { Engine, readSpec, specToCsv } = require('./cx.js');
const { toTree, toSpec, show, valid, mutator, makeRng } = require('./race.js');
const clone = t => JSON.parse(JSON.stringify(t));
(async () => {
  const [start, minutes, prefix, seed] = process.argv.slice(2), LAMBDA = 420;
  const rnd = makeRng(+(seed || 1)), mut = mutator(rnd, { fixroot: 1, oldonly: 0, nmin: 20, nmax: 22 }), eng = new Engine();
  const J = t => ({ ...toSpec(t), seed0: 11, count: 5, block: 5, qcut: 120 });
  const key = t => JSON.stringify(toSpec(t)), seen = new Set();
  let s0 = toTree(readSpec(start)); const p0 = (await eng.run([J(s0)]))[0][0];
  let elite = [{ t: s0, pub: p0 }]; seen.add(key(s0));
  const logf = prefix + '.log', log = s => { console.log(s); fs.appendFileSync(logf, s + '\n'); };
  const t0 = Date.now(), el = () => ((Date.now() - t0) / 60000).toFixed(1);
  log(`시작 ${start}: 공개 ${p0.toFixed(2)}`);
  let gen = 0, evals = 0;
  while ((Date.now() - t0) / 60000 < +minutes) {
    gen++; const pool = [];
    for (let tries = 0; pool.length < LAMBDA && tries < LAMBDA * 20; tries++) {
      const par = rnd() < 0.5 ? elite[0].t : elite[Math.floor(rnd() * elite.length)].t; let t; const r = rnd();
      if (r < 0.35) { t = clone(par); if (t.root.f === 'w') t.root.th = +(Math.floor(rnd() * 400) * 0.00001).toFixed(5); else continue; }
      else { t = mut(par); if (!t) continue; if (rnd() < 0.3 && t.root.f === 'w') t.root.th = +(Math.floor(rnd() * 400) * 0.00001).toFixed(5); }
      if (!valid(t)) continue; const k = key(t); if (seen.has(k)) continue; seen.add(k); pool.push({ t });
    }
    if (!pool.length) break;
    const r = await eng.run(pool.map(c => J(c.t))); evals += pool.length;
    pool.forEach((c, i) => { c.pub = r[i] ? r[i][0] : Infinity; });
    const before = elite[0].pub, have = new Set(elite.map(e => e.pub.toFixed(6)));
    for (const c of pool) if (Number.isFinite(c.pub) && !have.has(c.pub.toFixed(6))) { elite.push(c); have.add(c.pub.toFixed(6)); }
    elite.sort((a, b) => a.pub - b.pub); elite = elite.slice(0, 8);
    if (elite[0].pub < before - 1e-9) {
      fs.writeFileSync(prefix + '_best.csv', specToCsv(toSpec(elite[0].t)));
      log(`세대 ${gen} (${el()}분, 평가 ${evals}): 공개 ${before.toFixed(2)} → ${elite[0].pub.toFixed(2)} | n${elite[0].t.n} ${show(elite[0].t.root)}`);
    }
  }
  log(`끝 (${el()}분, ${gen}세대, 평가 ${evals}개): 최저 공개 ${elite[0].pub.toFixed(2)}`);
  fs.writeFileSync(prefix + '_elite.json', JSON.stringify(elite.map(e => ({ pub: e.pub, spec: toSpec(e.t), show: show(e.t.root) })), null, 1));
  eng.close();
})();
