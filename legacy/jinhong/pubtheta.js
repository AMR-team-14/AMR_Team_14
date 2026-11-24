// 공개 점수: 루트 w 경계값을 0~0.03까지 0.00001 간격으로 촘촘히 훑는다(지금까지는 0.003까지만 봤다).
// 경계값이 커지면 기대값은 조금 나빠지지만(0.01에서 약 +1, 0.02에서 약 +2) 서로 다른 결과가 훨씬 많이 나온다.
// 사용: node pubtheta.js <csv...>
const fs = require('fs');
const { Engine, readSpec, specToCsv } = require('./cx.js');
const { toTree, toSpec, show } = require('./race.js');
(async () => {
  const eng = new Engine(), files = process.argv.slice(2), t0 = Date.now(), all = [];
  const TH = Array.from({ length: 3001 }, (_, i) => +(i * 0.00001).toFixed(5));
  for (const f of files) {
    const base = toTree(readSpec(f)); if (base.root.f !== 'w') continue;
    const list = TH.map(th => { const t = JSON.parse(JSON.stringify(base)); t.root.th = th; return { t, th, f }; });
    const r = await eng.run(list.map(c => ({ ...toSpec(c.t), seed0: 11, count: 5, block: 5, qcut: 120 })));
    list.forEach((c, i) => { c.pub = r[i] ? r[i][0] : Infinity; });
    const ok = list.filter(c => Number.isFinite(c.pub)), d = new Set(ok.map(c => c.pub.toFixed(6))), best = ok.reduce((a, b) => a.pub <= b.pub ? a : b);
    console.log(`${f.split('/').pop()} n${base.n}: 서로 다른 값 ${d.size}개, 최저 ${best.pub.toFixed(2)} (경계값 ${best.th}) [${((Date.now() - t0) / 60000).toFixed(1)}분]`);
    all.push(...ok);
  }
  const seen = new Set(), top = all.sort((a, b) => a.pub - b.pub).filter(c => !seen.has(c.pub.toFixed(6)) && seen.add(c.pub.toFixed(6))).slice(0, 8);
  console.log('전체 최저 8개:');
  top.forEach((c, i) => { console.log(`  ${c.pub.toFixed(2)} | 경계값 ${c.th} | n${c.t.n} ${show(c.t.root)}`); fs.writeFileSync(`designs/pubtheta_${i + 1}.csv`, specToCsv(toSpec(c.t))); });
  eng.close();
})();
