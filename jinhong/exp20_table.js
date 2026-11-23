// 실험 20 (자문자답 1회전): 「제출 형식의 한계」 재기.
// 형식 밖 가상 정책: (차 위치 × 출발 부서) 우선순위 표를 나무보다 먼저 본다. 표는 도착 즉시용(tf) · 대기 후용(to) 두 장.
// 표 뒤의 동점 처리는 B2 나무 그대로다. 이 정책은 제출할 수 없다. 특성에 「출발 부서」가 있다면 얼마나 내려가는지 보는 용도다.
const fs = require('fs');
const { Engine, readSpec, paired, mean } = require('./cx.js');
const D = ['STOR-B', 'PICK', 'PACK', 'STOR-A', 'RECV', 'SORT', 'SHIP'], ix = n => D.indexOf(n);
const mk = (def, rules) => { const t = Array.from({ length: 7 }, () => new Array(7).fill(def)); for (const [v, o, r] of rules) t[ix(v)][ix(o)] = r; return t; };
const zero = () => mk(0, []);
// Gurobi 최적 흐름에 나온 칸(실험 18)
const LP = [['PICK', 'PICK', 0], ['PACK', 'PACK', 0], ['SORT', 'SORT', 0], ['STOR-A', 'STOR-B', 0], ['SHIP', 'RECV', 0], ['STOR-A', 'RECV', 0], ['RECV', 'RECV', 0], ['SHIP', 'SHIP', 0]];
const lp = () => mk(1, LP);
// 순환 지키기: 내린 자리 주문이 1순위, 남는 차(STOR-A · SHIP)가 2순위, 다른 부서의 순환 차는 마지막
const keep = shipFirst => { const t = mk(2, []);
  for (const o of ['PICK', 'PACK', 'SORT', 'SHIP', 'RECV']) t[ix(o)][ix(o)] = 0;
  for (const o of ['PICK', 'PACK', 'SORT', 'RECV']) { t[ix('STOR-A')][ix(o)] = 1; t[ix('SHIP')][ix(o)] = 1; }
  t[ix('SHIP')][ix('RECV')] = 0; t[ix('STOR-A')][ix('RECV')] = 0; t[ix('RECV')][ix('STOR-B')] = 1;
  t[ix('STOR-A')][ix('STOR-B')] = shipFirst ? 1 : 0; t[ix('SHIP')][ix('STOR-B')] = shipFirst ? 0 : 1; return t; };
const enc = (tf, to) => 'T' + [...tf.flat(), ...to.flat()].join('.') + '#';
(async () => {
  const eng = new Engine(), base = readSpec('designs/b2_n21.csv'), NB = 60, c = [];
  const add = (name, tf, to, n = 21) => c.push({ name, n, nodes: enc(tf, to) + base.nodes });
  add('표 전부 0 (B2와 같아야 함)', zero(), zero());
  add('LP 칸 먼저: 도착 · 대기 둘 다', lp(), lp());
  add('LP 칸 먼저: 도착만', lp(), zero());
  add('LP 칸 먼저: 대기만', zero(), lp());
  add('순환 지키기(STOR-B는 STOR-A 먼저): 도착만', keep(false), zero());
  add('순환 지키기(STOR-B는 SHIP 먼저): 도착만', keep(true), zero());
  add('순환 지키기(SHIP 먼저): 도착 · 대기 둘 다', keep(true), keep(true));
  for (const n of [18, 19, 20]) { add(`순환 지키기(SHIP 먼저) 도착만 n${n}`, keep(true), zero(), n); add(`LP 칸 먼저 둘 다 n${n}`, lp(), lp(), n); }
  const J = s => ({ n: s.n, nodes: s.nodes, seed0: 100001, count: NB * 5, block: 5, qcut: 150 });
  const [b, ...r] = await eng.run([J(base), ...c.map(J)]);
  const out = [`기준 B2(21대) 300시드 평균 ${mean(b).toFixed(2)}`];
  c.forEach((x, i) => { const v = r[i]; if (!v.every(Number.isFinite)) return out.push(`${x.name}: 반려 발생`); const p = paired(v, b); out.push(`${(p.d >= 0 ? '+' : '') + p.d.toFixed(2)} ± ${p.se.toFixed(2)} (평균 ${mean(v).toFixed(2)})  ${x.name}`); });
  console.log(out.join('\n')); fs.writeFileSync('runs/exp20_table.txt', out.join('\n') + '\n'); eng.close();
})();
