// mapdir — 격자 지도의 방향을 글자 그림으로 찍는다(가로줄은 > <, 세로줄은 ^ v, 대각은 따로).  node mapdir.js <케이스> <csv>
const { loadCase, readSpec } = require('./gx.js');
const [c, f] = process.argv.slice(2), P = loadCase(c), bits = readSpec(f).dir, X = P.nodes.map(n => n[1]), Y = P.nodes.map(n => n[2]), nm = P.nodes.map(n => n[0]);
const xs = [...new Set(X)].sort((a, b) => a - b), ys = [...new Set(Y)].sort((a, b) => b - a);
const at = (x, y) => P.nodes.findIndex((n, i) => X[i] === x && Y[i] === y);
const dirOf = k => { const e = P.edges[k]; return bits[k] === '1' ? [e[1], e[0]] : [e[0], e[1]]; };
const H = new Map(), V = new Map(), diag = [];
P.edges.forEach((e, k) => { const [a, b] = dirOf(k); if (Y[a] === Y[b]) H.set(Math.min(X[a], X[b]) + ',' + Y[a], X[b] > X[a] ? '>' : '<'); else if (X[a] === X[b]) V.set(X[a] + ',' + Math.max(Y[a], Y[b]), Y[b] > Y[a] ? '^' : 'v'); else diag.push(`${nm[a]}→${nm[b]}`); });
const cell = (x, y) => { const i = at(x, y); return (i < 0 ? '' : nm[i].replace(/^G.*/, '+')).padEnd(3).slice(0, 3); };
for (let r = 0; r < ys.length; r++) {
  let line = ''; for (let q = 0; q < xs.length; q++) { line += at(xs[q], ys[r]) >= 0 ? cell(xs[q], ys[r]) : '   '; if (q < xs.length - 1) line += (H.get(xs[q] + ',' + ys[r]) || ' ') + ' '; }
  console.log(String(ys[r]).padStart(5) + ' ' + line);
  if (r < ys.length - 1) { let l2 = ''; for (let q = 0; q < xs.length; q++) l2 += (V.get(xs[q] + ',' + ys[r]) || ' ').padEnd(5); console.log('      ' + l2); }
}
console.log('      x: ' + xs.join(' ')); console.log('대각: ' + diag.join('  '));
