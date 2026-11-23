// GitHub Actions 결과 모으기: gh run download <run id> -D gha/dl 로 받은 조각들을 합쳐 공개 점수 최저안을 뽑고,
// 상위 후보를 새 시드 1,000개로 돌려 반려 여부와 평균을 같이 보여 준다.
// 사용: node gha_collect.js [받은 폴더=gha/dl] [새 시드 시작=800001]
const fs = require('fs'), path = require('path');
const { Engine, specToCsv, mean } = require('./cx.js');
(async () => {
  const dir = process.argv[2] || 'gha/dl', s0 = +(process.argv[3] || 800001), files = [];
  (function walk(d) { for (const f of fs.readdirSync(d)) { const p = path.join(d, f); fs.statSync(p).isDirectory() ? walk(p) : p.endsWith('.json') && files.push(p); } })(dir);
  const seen = new Set(); let all = [], evals = 0;
  for (const f of files) { const r = JSON.parse(fs.readFileSync(f, 'utf8')); evals += r.evals || 0; for (const c of r.top) if (!seen.has(c.pub.toFixed(6))) { seen.add(c.pub.toFixed(6)); all.push(c); } }
  all.sort((a, b) => a.pub - b.pub);
  console.log(`조각 ${files.length}개, 평가 합계 ${evals}개, 서로 다른 상위 후보 ${all.length}개. 최저 ${all[0].pub.toFixed(2)}`);
  const top = all.slice(0, 8), eng = new Engine();
  const pub = await eng.run(top.map(c => ({ ...c.spec, seed0: 11, count: 5, block: 5 })));
  const chk = await eng.run(top.map(c => ({ ...c.spec, seed0: s0, count: 1000, block: 5 })));
  top.forEach((c, i) => {
    const bad = chk[i].filter(x => !Number.isFinite(x)).length, ok = chk[i].filter(Number.isFinite);
    console.log(`공개 ${c.pub.toFixed(2)} (로컬 재계산 ${pub[i][0].toFixed(2)}) | 새 시드 평균 ${mean(ok).toFixed(2)}, 반려 묶음 ${bad}/200 | n${c.spec.n} ${c.show}`);
    fs.writeFileSync(`gha/best_${i + 1}.csv`, specToCsv(c.spec));
  });
  eng.close();
})();
