// 후보 목록을 기준안과 같은 시드 묶음에서 짝비교하며 단계적으로 줄인다(race.js의 선별 부분만 떼어 낸 것).
const { paired, mean } = require('./cx.js');
const { toSpec, show } = require('./race.js');
const N = (f, d, th, lo = null, hi = null) => ({ f, d, th, lo, hi });
// stages: [[누적 묶음 수, 통과 기준(짝차이 평균 < 값, null이면 모두 남김)], ...]
async function raceList(eng, base, cands, { stages = [[4, 3], [16, 1], [60, null]], seed0 = 100001, qcut = 110 } = {}) {
  const maxB = stages[stages.length - 1][0];
  const job = (t, b0, b1, qc) => ({ ...toSpec(t), seed0: seed0 + 5 * b0, count: 5 * (b1 - b0), block: 5, qcut: qc });
  const baseCost = (await eng.run([job(base, 0, maxB, 0)]))[0];
  let pool = cands.map(c => ({ ...c, cost: [] })), prev = 0; const dropped = [];
  for (const [nb, thr] of stages) {
    const res = await eng.run(pool.map(c => job(c.t, prev, nb, qcut)));
    pool.forEach((c, i) => { c.cost = c.cost.concat(res[i] || [Infinity]); c.ok = c.cost.every(Number.isFinite); c.blocks = nb; if (c.ok) c.p = paired(c.cost, baseCost.slice(0, nb)); });
    const keep = pool.filter(c => c.ok && (thr === null || c.p.d < thr));
    pool.filter(c => !keep.includes(c)).forEach(c => dropped.push(c));
    pool = keep; prev = nb; if (!pool.length) break;
  }
  pool.sort((a, b) => a.p.d - b.p.d);
  return { baseCost, baseMean: mean(baseCost), final: pool, dropped };
}
const fmt = c => `${c.p.d >= 0 ? '+' : ''}${c.p.d.toFixed(2)} ± ${c.p.se.toFixed(2)} (t ${c.p.t.toFixed(1)}, ${c.blocks * 5}시드)`;
module.exports = { raceList, N, fmt, show, toSpec };
