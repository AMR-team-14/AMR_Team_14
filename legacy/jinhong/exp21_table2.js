// 실험 21 (자문자답 2회전): 59점이 어느 규칙에서 나오는지 쪼개기 + 대수별 확인. 모두 형식 밖 가상 정책이다(도착 즉시 표만 씀).
const fs = require('fs');
const { Engine, readSpec, paired, mean } = require('./cx.js');
const D = ['STOR-B', 'PICK', 'PACK', 'STOR-A', 'RECV', 'SORT', 'SHIP'], ix = n => D.indexOf(n);
const mk = (def, rules) => { const t = Array.from({ length: 7 }, () => new Array(7).fill(def)); for (const [v, o, r] of rules) t[ix(v)][ix(o)] = r; return t; };
const zero = () => mk(0, []);
const col = (o, ranks) => { const t = zero(); D.forEach(v => { t[ix(v)][ix(o)] = ranks[v] ?? 1; }); return t; };
const add2 = (a, b) => a.map((r, i) => r.map((x, j) => x + b[i][j]));
const recvSink = col('RECV', { SHIP: 0, 'STOR-A': 0, RECV: 0 });            // RECV 주문: 남는 차(SHIP · STOR-A) 먼저
const recvShip = col('RECV', { SHIP: 0, RECV: 0 });                         // RECV 주문: SHIP 차만 먼저
const storbA = col('STOR-B', { 'STOR-A': 0 });                              // STOR-B 주문: STOR-A 차 먼저(그다음은 나무의 SHIP 먼저)
const chain = (() => { let t = zero(); for (const o of ['PICK', 'PACK', 'SORT']) t = add2(t, col(o, { [o]: 0, 'STOR-A': 1, SHIP: 1, RECV: 1, 'STOR-B': 1, PICK: 2, PACK: 2, SORT: 2 })); for (const o of ['PICK', 'PACK', 'SORT']) t[ix(o)][ix(o)] = 0; return t; })(); // 순환 부서 주문: 자기 자리 차 → 남는 차 → 다른 순환 차
const enc = (tf, to) => 'T' + [...tf.flat(), ...to.flat()].join('.') + '#';
(async () => {
  const eng = new Engine(), base = readSpec('designs/b2_n21.csv'), NB = 60, c = [];
  const add = (name, tf, n = 21) => c.push({ name, n, nodes: enc(tf, zero()) + base.nodes });
  add('① RECV 주문에 남는 차(SHIP · STOR-A) 먼저', recvSink);
  add('② RECV 주문에 SHIP 차만 먼저', recvShip);
  add('③ STOR-B 주문에 STOR-A 차 먼저', storbA);
  add('④ 순환 부서(PICK · PACK · SORT) 주문: 다른 순환 차는 마지막', chain);
  add('① + ③', add2(recvSink, storbA));
  add('① + ④', add2(recvSink, chain));
  add('① + ③ + ④', add2(add2(recvSink, storbA), chain));
  for (const n of [19, 20, 22]) { add(`① n${n}`, recvSink, n); add(`① + ③ + ④ n${n}`, add2(add2(recvSink, storbA), chain), n); }
  const J = s => ({ n: s.n, nodes: s.nodes, seed0: 100001, count: NB * 5, block: 5, qcut: 150 });
  const [b, ...r] = await eng.run([J(base), ...c.map(J)]);
  const out = [`기준 B2(21대) 300시드 평균 ${mean(b).toFixed(2)}`];
  c.forEach((x, i) => { const v = r[i]; if (!v.every(Number.isFinite)) return out.push(`${x.name}: 반려 발생`); const p = paired(v, b); out.push(`${(p.d >= 0 ? '+' : '') + p.d.toFixed(2)} ± ${p.se.toFixed(2)} (평균 ${mean(v).toFixed(2)})  ${x.name}`); });
  console.log(out.join('\n')); fs.writeFileSync('runs/exp21_table2.txt', out.join('\n') + '\n'); eng.close();
})();
