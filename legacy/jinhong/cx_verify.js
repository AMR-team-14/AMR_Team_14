// C++ 엔진이 웹 원본 JS 엔진과 같은 값을 내는지 대조한다.
// 설계 여러 개 × 시드 묶음 여러 개의 묶음 비용을 양쪽에서 계산해 차이의 최댓값을 본다.
// 사용: node cx_verify.js [csv...]
const fs = require('fs');
const E = require('./web/engine_raw.js');
const { parse } = require('./eval.js');
const { Engine, readSpec } = require('./cx.js');
const D = E.DATA_AGV;

function webBlock(design, seeds) {
  const r = E.agvEvaluate_(D, design, seeds);
  return r.stable ? r.fleetCost + r.waitCost + r.urgCost : Infinity;
}
(async () => {
  const files = process.argv.slice(2).length ? process.argv.slice(2) : fs.readdirSync('designs').filter(f => f.endsWith('.csv')).map(f => 'designs/' + f);
  const eng = new Engine(), BL = [[11, 5], [101, 5], [2001, 5], [777777, 5]];
  let worst = 0, n = 0; const t0 = Date.now();
  for (const f of files) {
    const design = parse(fs.readFileSync(f, 'utf8')), spec = readSpec(f);
    const cpp = await eng.run(BL.map(([s, c]) => ({ ...spec, seed0: s, count: c, block: c })));
    BL.forEach(([s, c], i) => {
      const w = webBlock(design, Array.from({ length: c }, (_, k) => s + k)), x = cpp[i][0];
      const diff = (w === Infinity && x === Infinity) ? 0 : Math.abs(w - x);
      if (diff > worst) worst = diff; n++;
      if (diff > 1e-9) console.log(`다름! ${f} 시드 ${s}: 웹 ${w} / C++ ${x}`);
    });
  }
  console.log(`${files.length}개 설계 × ${BL.length}개 묶음 = ${n}건 대조, 최대 차이 ${worst.toExponential(2)}, ${((Date.now() - t0) / 1000).toFixed(1)}초`);
  eng.close();
})();
