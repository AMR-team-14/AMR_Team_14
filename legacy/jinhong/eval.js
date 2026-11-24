// 웹앱 채점 코드(web/engine_raw.js, 웹 페이지에서 그대로 잘라 옴)로 answer.csv를 평가한다.
// 사용: node eval.js <csv...> [--seeds 11-15] [--n 18,19,20]
const fs = require('fs');
const E = require('./web/engine_raw.js');

function seedsOf(s) {
  const [a, b] = s.split('-').map(Number);
  return b ? Array.from({ length: b - a + 1 }, (_, i) => a + i) : s.split(',').map(Number);
}
function parse(text) {
  const r = E.agvParse_(E.DATA_AGV, text);
  if (!r.design) throw new Error(JSON.stringify(r).slice(0, 300));
  return r.design;
}
function evalText(text, seeds) {
  return E.agvEvaluate_(E.DATA_AGV, parse(text), seeds);
}
function withN(text, n) { return text.replace(/^fleet,\d+/m, 'fleet,' + n); }
function fmt(r) {
  if (!r.stable) return `반려(시드 ${r.unstableSeed}, endQ ${r.endQ})`;
  return `총 ${r.value.toFixed(2)} | W̄ ${r.mean.toFixed(4)} | U95 ${r.u95.toFixed(4)}`;
}
module.exports = { evalText, withN, seedsOf, parse, fmt };

if (require.main === module) {
  const args = process.argv.slice(2);
  let seeds = [11, 12, 13, 14, 15], ns = null; const files = [];
  for (let i = 0; i < args.length; i++) {
    if (args[i] === '--seeds') seeds = seedsOf(args[++i]);
    else if (args[i] === '--n') ns = args[++i].split(',').map(Number);
    else files.push(args[i]);
  }
  for (const f of files) {
    const text = fs.readFileSync(f, 'utf8');
    for (const n of ns || [null]) {
      const t = n ? withN(text, n) : text;
      const r = evalText(t, seeds);
      console.log(`${f.split(/[\\/]/).pop()}${n ? ' n=' + n : ''} [${seeds[0]}..${seeds[seeds.length - 1]}] ${fmt(r)}`);
    }
  }
}
