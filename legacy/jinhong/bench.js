// 엔진 속도 재기: node bench.js [스레드 수] [시드 수]
const { Engine, readSpec, mean } = require('./cx.js');
(async () => {
  const th = +(process.argv[2] || 14), N = +(process.argv[3] || 1000), eng = new Engine(th), a = readSpec('answer_jinhong_submitted.csv');
  await eng.run([{ ...a, seed0: 1, count: 20, block: 5 }]);
  const t0 = Date.now(), r = await eng.run([{ ...a, seed0: 100001, count: N, block: 5 }]), dt = (Date.now() - t0) / 1000;
  console.log(`스레드 ${th}: ${N}시드 ${dt.toFixed(2)}초 → 초당 ${(N / dt).toFixed(0)}시드 | 평균 ${mean(r[0]).toFixed(4)}`);
  eng.close();
})();
