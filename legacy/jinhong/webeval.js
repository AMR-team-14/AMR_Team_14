// webeval — 웹 원본 JS(agvParse_ · agvEvaluate_)로 CSV를 직접 채점한다(공개 시드). C++ 엔진 값과 같은지 제출 전에 본다.
//  공유 나무(같은 기준 id를 여러 갈래가 가리키는 CSV)도 웹 검사를 그대로 거친다.   node webeval.js <N> <csv...>
const fs = require('fs'), path = require('path');
const { loadCase } = require('./gx.js');
const W = require(path.join(__dirname, 'web', 'engine_final.js'));
const [c, ...files] = process.argv.slice(2), P = loadCase(c);
for (const f of files) {
  const txt = fs.readFileSync(f, 'utf8'), ps = W.agvParse_(P, txt), keys = txt.split(/\r?\n/).filter(l => l.startsWith('key')).length;
  if (!ps.design) { console.log(`${path.basename(f)}: 웹 형식 오류 — ${ps.errs.join(' / ')}`); continue; }
  const r = W.agvEvaluate_(P, ps.design, P.seeds);
  console.log(`${path.basename(f)}: 기준 ${keys}개 · 대수 ${ps.design.n} · ` + (r.stable ? `웹 원본 공개 점수 ${(r.fleetCost + r.waitCost + r.urgCost).toFixed(4)} (대수 ${r.fleetCost.toFixed(2)} + 대기 ${r.waitCost.toFixed(2)} + 긴급 p95 ${r.urgCost.toFixed(2)})` : `반려 ${JSON.stringify(r)}`));
}
