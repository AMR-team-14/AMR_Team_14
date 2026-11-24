// 시드별로 한 번씩 돌려 대기시간을 모은 뒤, 5시드 묶음마다 웹과 같은 방식(합쳐서 평균 · 95분위)으로 비용을 낸다.
// 비공개 시드가 5개라고 가정할 때 후보 간 순위가 얼마나 흔들리는지 본다.
// 사용: node blocks.js <csv...> --seeds 101-150
const fs = require('fs');
const E = require('./web/engine_raw.js');
const { parse, seedsOf } = require('./eval.js');
const D = E.DATA_AGV;
const lam = D.alpha * D.flows.reduce((s, f) => s + f[2], 0);

function perSeed(design, seeds) {
  const net = E.agvNet_(D, design.dirs);
  return seeds.map(s => { const o = E.agvRun_(D, net, design, s); return { s, ws: o.ws, wu: o.wu, unstable: o.unstable }; });
}
function cost(n, runs) {
  if (runs.some(r => r.unstable)) return Infinity;
  const ws = runs.flatMap(r => r.ws), wu = runs.flatMap(r => r.wu).sort((a, b) => a - b);
  const mean = ws.reduce((a, b) => a + b, 0) / ws.length;
  const u95 = wu.length ? wu[Math.floor(0.95 * (wu.length - 1))] : 0;
  return D.cAGV * n + D.cW * lam * mean + D.cU * u95;
}

const args = process.argv.slice(2); let seeds = seedsOf('101-150'); const files = [];
for (let i = 0; i < args.length; i++) args[i] === '--seeds' ? (seeds = seedsOf(args[++i])) : files.push(args[i]);
const res = files.map(f => { const d = parse(fs.readFileSync(f, 'utf8')); return { f: f.split(/[\\/]/).pop(), n: d.n, runs: perSeed(d, seeds) }; });
const nb = Math.floor(seeds.length / 5), table = res.map(r => []);
for (let b = 0; b < nb; b++) res.forEach((r, i) => table[i].push(cost(r.n, r.runs.slice(b * 5, b * 5 + 5))));
res.forEach((r, i) => {
  const v = table[i], m = v.reduce((a, b) => a + b, 0) / v.length, sd = Math.sqrt(v.reduce((a, b) => a + (b - m) ** 2, 0) / (v.length - 1));
  const wins = v.filter((x, b) => table.every(t => t[b] >= x)).length;
  console.log(`${r.f}: 전체 ${cost(r.n, r.runs).toFixed(2)} | 5시드 묶음 ${nb}개 평균 ${m.toFixed(2)} ± ${sd.toFixed(2)} | 1등 ${wins}/${nb}`);
});
console.log('묶음별:', table.map(v => v.map(x => x.toFixed(0)).join(' ')).join('\n        '));
