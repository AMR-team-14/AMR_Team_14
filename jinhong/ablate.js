// ablate — night_dirpf.json의 형식 밖 최저 나무에서 de(max,r) 잎을 하나씩 빼고(가까운 차 먼저로) 점수 변화를 본다. 어느 묶음의 반경이 실제로 일하는지 가린다.
//   node ablate.js <N> <night_dirpf.json 안의 순번(1부터)> [대수들 a,b,c]
const fs = require('fs'), path = require('path');
const { Engine, loadCase } = require('./gx.js');
const [CN, IDX = '1', NS] = process.argv.slice(2), P = loadCase(CN), OUT = path.join(__dirname, 'case' + CN);
const rec = JSON.parse(fs.readFileSync(path.join(OUT, 'night_dirpf.json'), 'utf8'))[+IDX - 1];
function parse(str) { let i = 0; const node = () => { if (str[i] === '-') { i++; return null; } const m = /^([a-z]+)\((min|max),([-0-9.]+)\)/.exec(str.slice(i)); i += m[0].length; const nd = { f: m[1], d: m[2], th: +m[3], lo: null, hi: null }; if (str[i] === '[') { i++; nd.lo = node(); i++; nd.hi = node(); i++; } return nd; }; return node(); }
const show = nd => !nd ? '-' : (nd.lo || nd.hi ? `${nd.f}(${nd.d},${nd.th})[${show(nd.lo)},${show(nd.hi)}]` : `${nd.f}(${nd.d},${nd.th})`);
const spec = (root, n) => { const out = []; let next = 1; const q = [[root, next++]]; while (q.length) { const [nd, id] = q.shift(); const lo = nd.lo ? next++ : 0, hi = nd.hi ? next++ : 0; out.push(`${id},${nd.f},${nd.d},${nd.th},${lo},${hi}`); if (nd.lo) q.push([nd.lo, lo]); if (nd.hi) q.push([nd.hi, hi]); } return { dir: rec.dir, n, nodes: out.join(';'), seed0: P.seeds[0], count: P.seeds.length, block: P.seeds.length, qcut: 150 }; };
(async () => {
  const eng = new Engine(CN, +(process.env.AGVX_THREADS || 4)), ns = NS ? NS.split(',').map(Number) : [rec.n];
  const base = parse(rec.show), fresh = base.lo;   // 루트 w의 lo = 방금 들어온 주문 쪽
  const leaves = []; (function walk(nd, par, side, lo, hi) { if (!nd) return; if (nd.f === 'de') { leaves.push({ par, side, r: nd.th, lo, hi }); return; } if (nd.f === 'dl') { walk(nd.lo, nd, 'lo', lo, nd.th); walk(nd.hi, nd, 'hi', nd.th, hi); } else { walk(nd.lo, nd, 'lo', lo, hi); walk(nd.hi, nd, 'hi', lo, hi); } })(fresh, base, 'lo', 0, Infinity);
  const ev = async root => (await eng.run(ns.map(n => spec(root, n)))).map(x => x ? x[0] : Infinity);
  const b0 = await ev(base); console.log(`기준 ${rec.dir} ${ns.map((n, i) => `n${n}:${b0[i].toFixed(2)}`).join(' ')}`);
  for (const L of leaves) { const keep = L.par[L.side]; L.par[L.side] = null; const r = await ev(base); L.par[L.side] = keep; console.log(`  dl (${L.lo}, ${L.hi}] 반경 ${L.r} 빼면: ${ns.map((n, i) => `n${n}:${r[i].toFixed(2)}(${(r[i] - b0[i] >= 0 ? '+' : '') + (r[i] - b0[i]).toFixed(2)})`).join(' ')}`); }
  { const keep = base.hi; base.hi = parse('urg(max,0.5)'); const r = await ev(base); base.hi = keep; console.log(`  대기 가지를 urg(max) 하나로: ${ns.map((n, i) => `n${n}:${r[i].toFixed(2)}`).join(' ')}`); }
  eng.close();
})();
