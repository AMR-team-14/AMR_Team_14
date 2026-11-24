// mkvar — 기준 방향에서 통로 몇 개의 방향을 「노드 이름 a→b」로 지정해 바꾼 변형을 만든다(강연결 검사 포함).
//  node mkvar.js <케이스> <기준 csv> <출력 csv> "G1.0>S7,S7>G0.0,..."
const fs = require('fs'); const { loadCase, strong, readSpec, specToCsv } = require('./gx.js');
const [c, f, out, spec] = process.argv.slice(2), P = loadCase(c), s = readSpec(f), nm = P.nodes.map(n => n[0]), b = s.dir.split('');
for (const pr of spec.split(',')) { const [a, z] = pr.split('>'), ia = nm.indexOf(a), iz = nm.indexOf(z); const k = P.edges.findIndex(e => (e[0] === ia && e[1] === iz) || (e[0] === iz && e[1] === ia)); if (ia < 0 || iz < 0 || k < 0) { console.log('통로 없음: ' + pr); process.exit(1); } b[k] = P.edges[k][0] === ia ? '0' : '1'; }
const d = b.join(''); let diff = 0; for (let i = 0; i < d.length; i++) if (d[i] !== s.dir[i]) diff++;
console.log(`${out}: 바꾼 통로 ${diff}개, 강연결 ${strong(P, d) ? '예' : '아니오'}`); if (strong(P, d)) fs.writeFileSync(out, specToCsv({ ...s, dir: d }));
