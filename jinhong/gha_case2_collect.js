// GitHub Actions Case 2 결과 모으기: gh run download <실행 번호> -D gha/dl2 로 받은 조각들을 합쳐 공개 점수가 낮은 순으로 보여 준다.
// 조각이 남긴 search_s<씨앗>_best.csv 와 _elite.json 의 후보를 로컬 엔진으로 다시 계산한다(케이스 데이터가 로컬에 있어야 한다).
// 상위 안은 case2/gha_top_<i>.csv 로 쓴다. 엔진은 형식을 보지 않으므로 그 뒤에 반드시 pipe.js eval · safety 와 웹 「평가 실행」 대조를 거친다.
// 사용: node gha_case2_collect.js [받은 폴더=gha/dl2] [상위 몇 개=5]
const fs = require('fs'), path = require('path');
const { Engine, csvToSpec, specToCsv, round2 } = require('./gx.js');
(async () => {
  const dir = process.argv[2] || 'gha/dl2', top = +(process.argv[3] || 5), files = [];
  (function walk(d) { for (const f of fs.readdirSync(d)) { const p = path.join(d, f); fs.statSync(p).isDirectory() ? walk(p) : files.push(p); } })(dir);
  const cand = new Map(); let shards = 0, evals = 0, failed = 0;
  const add = (spec, from) => { const k = `${spec.dir}|${spec.n}|${spec.nodes}`; if (!cand.has(k)) cand.set(k, { spec, from }); };
  for (const f of files) {
    const b = path.basename(f);
    if (/^shard_\d+\.json$/.test(b)) { const r = JSON.parse(fs.readFileSync(f, 'utf8')); shards++; evals += r.evals || 0; failed += r.rounds.filter(x => x.pub === null).length; }
    else if (/_best\.csv$/.test(b)) add(csvToSpec(fs.readFileSync(f, 'utf8')), b);
    else if (/_elite\.json$/.test(b)) for (const e of JSON.parse(fs.readFileSync(f, 'utf8')).slice(0, 8)) add(e.spec, b);
  }
  if (!cand.size) { console.log(`${dir} 에 후보가 없다(조각 ${shards}개).`); return; }
  const eng = new Engine(2), list = [...cand.values()];
  const ref = (await eng.pub([csvToSpec(fs.readFileSync(path.join(__dirname, 'case2', 'answer_case2.csv'), 'utf8'))]))[0];
  (await eng.pub(list.map(c => c.spec))).forEach((v, i) => { list[i].pub = v; });
  eng.close();
  const seen = new Set(), ok = list.filter(c => Number.isFinite(c.pub)).sort((a, b) => a.pub - b.pub).filter(c => !seen.has(c.pub.toFixed(6)) && seen.add(c.pub.toFixed(6)));
  console.log(`조각 ${shards}개(실패한 회차 ${failed}개), 평가 합계 ${evals}개, 서로 다른 후보 ${list.length}개(점수가 다른 것 ${ok.length}개). 제출안 ${ref.toFixed(2)}, 최저 ${ok[0].pub.toFixed(2)}`);
  ok.slice(0, top).forEach((c, i) => {
    const out = path.join(__dirname, 'case2', `gha_top_${i + 1}.csv`); fs.writeFileSync(out, specToCsv(c.spec));
    console.log(`${i + 1}. 공개 ${c.pub.toFixed(2)}${c.pub < ref - 1e-9 ? ` (제출안보다 ${(ref - c.pub).toFixed(2)} 낮음)` : ''} | ${c.spec.n}대, 기준 ${c.spec.nodes.split(';').length}개 | ${c.from} → case2/gha_top_${i + 1}.csv`);
  });
  console.log(ok[0].pub < ref - 1e-9
    ? `다음: node pipe.js 2 eval case2/gha_top_1.csv (웹 형식 검사) → node pipe.js 2 safety case2/gha_top_1.csv 900001 1000 (반려 0건) → 웹 「평가 실행」 값 ${round2(ok[0].pub)} 대조. 제출은 진홍이 정한다.`
    : '제출안보다 낮은 안이 없다.');
})().catch(e => { console.error(e); process.exit(1); });
