// 웹 원본 엔진(web/engine_raw.js)에서 케이스 데이터와 최단거리 · 다음 노드 표를 뽑아 cpp/case.txt로 쓴다.
// 통로 방향이 고정이라 표는 한 번만 만들면 된다. C++ 엔진(agvx.cpp)이 이 파일을 읽는다.
const fs = require('fs'), path = require('path');
const E = require('../web/engine_raw.js');
const D = E.DATA_AGV, net = E.agvNet_(D, D.fixedDir.split('').map(Number));
const N = D.nodes.length, deg = new Array(N).fill(0);
D.edges.forEach(e => { deg[e[0]]++; deg[e[1]]++; });
const J = []; for (let n = 0; n < N; n++) if (deg[n] >= 3) J.push(n);
const L = [];
L.push(`${N} ${D.pd.length}`);
L.push(D.pd.join(' '));
for (let i = 0; i < N; i++) L.push(net.D[i].join(' '));
for (let i = 0; i < N; i++) L.push(net.NX[i].join(' '));
L.push(`${J.length} ${J.join(' ')}`);
L.push(`${D.flows.length}`);
D.flows.forEach(f => L.push(f.join(' ')));
L.push([D.urgentDest, D.alpha, D.vc, D.ve, D.tl, D.tu, D.at, D.p, D.T, D.warm, D.qcap, D.endQmax, D.cAGV, D.cW, D.cU].join(' '));
L.push(D.depts.join(' '));
fs.writeFileSync(path.join(__dirname, 'case.txt'), L.join('\n') + '\n');
console.log('cpp/case.txt 저장', N, '노드,', J.length, '교차로');
