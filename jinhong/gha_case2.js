// GitHub Actions용 Case 2 공개 점수 탐색(조각 실행). pipe.js의 search를 씨앗만 바꿔 여러 번 부른다(pipe.js는 고치지 않는다).
//  0) 제출안(case2/answer_case2.csv)이 617.16으로 나오는지 먼저 확인한다. 다르면 엔진이나 케이스 데이터가 다른 것이므로 멈춘다.
//  1) <회당 분>짜리 탐색을 시간이 남는 동안 되풀이한다. 씨앗 = 2000 + 조각 × 50 + 회차(로컬에서 쓴 1~6과 겹치지 않는다).
//     시작안은 (조각 + 회차) % 3 으로 세 묶음을 번갈아 쓴다: 0 제출안만 / 1 + 다른 방향 계열의 구조안 / 2 + 방향 순위 상위 10개.
//     617.16보다 낮은 안이 나오면 다음 회차의 시작안에 넣는다.
//  2) 회차마다 search_s<씨앗>_best.csv · _elite.json 과 로그를 gha/out/c2/ 에 모은다(워크플로가 아티팩트로 올린다).
// 케이스 데이터는 리포에 없다. 환경변수 AGV_CASES 폴더의 final-agv-c2.json 을 읽는다(gx.js).
// 사용: node gha_case2.js <조각> <조각 수> <전체 분> [회당 분=60]
const fs = require('fs'), path = require('path'), { spawn } = require('child_process');
const { Engine, csvToSpec, round2 } = require('./gx.js');
const CASE = process.env.GHA_CASE || '2', EXPECT = +(process.env.GHA_EXPECT || 617.16);   // GHA_CASE: 로컬 시험용(json 경로를 주면 pipe.js가 다른 폴더에 쓴다)
const TAG = /\.json$/.test(CASE) ? path.basename(CASE, '.json').replace(/[^A-Za-z0-9]/g, '') : 'case' + CASE;   // pipe.js의 출력 폴더 규칙과 같다
const OUT = path.join(__dirname, TAG), SRC = path.join(__dirname, 'case2'), DST = path.join(__dirname, 'gha', 'out', 'c2');
const src = names => names.map(n => path.join(SRC, n)).filter(f => fs.existsSync(f));
const STARTS = [
  src(['answer_case2.csv', 'cand_617.csv']),
  src(['answer_case2.csv', 'cand_617.csv', 'struct_el_1.csv', 'struct_u30.csv', 'struct_xfer_5.csv']),
  src(['answer_case2.csv', 'cand_617.csv', ...Array.from({ length: 10 }, (_, i) => `dirtop_${i + 1}.csv`)]),
];
async function pubOf(file) { const eng = new Engine(CASE); const v = (await eng.pub([csvToSpec(fs.readFileSync(file, 'utf8'))]))[0]; eng.close(); return v; }
function runSearch(minutes, seed, starts, logFile) {
  return new Promise(res => {
    const p = spawn(process.execPath, ['pipe.js', CASE, 'search', String(minutes), String(seed), ...starts], { cwd: __dirname, stdio: ['ignore', 'pipe', 'inherit'] });
    let text = ''; p.stdout.on('data', d => { text += d; process.stdout.write(d); fs.appendFileSync(logFile, d); });
    p.on('close', code => res({ code, text }));
  });
}
(async () => {
  const [shard, nsh, minutes, per = 60] = process.argv.slice(2).map(Number), t0 = Date.now();
  const left = () => minutes - (Date.now() - t0) / 60000;
  fs.mkdirSync(DST, { recursive: true });
  const logFile = path.join(DST, `shard_${shard}.log`), rounds = [], extra = [];
  const say = s => { console.log(s); fs.appendFileSync(logFile, s + '\n'); };
  const base = await pubOf(path.join(SRC, 'answer_case2.csv'));
  say(`[조각 ${shard}/${nsh}] 제출안 공개 점수 ${base.toFixed(4)} (기대 ${EXPECT}), 전체 ${minutes}분, 회당 ${per}분`);
  if (round2(base) !== EXPECT) { say('제출안 점수가 기대값과 다르다: 엔진 빌드나 케이스 데이터를 확인. 탐색하지 않고 멈춘다.'); process.exit(1); }
  let best = { pub: base, seed: 0, csv: fs.readFileSync(path.join(SRC, 'answer_case2.csv'), 'utf8') }, evals = 0;
  const save = tag => fs.writeFileSync(path.join(DST, `shard_${shard}.json`), JSON.stringify({ shard, nsh, tag, expect: EXPECT, evals, minutes: +((Date.now() - t0) / 60000).toFixed(1), best, rounds }, null, 1));
  for (let r = 0; left() >= Math.min(per, 2); r++) {
    const seed = 2000 + shard * 50 + r, kind = (shard + r) % 3, starts = [...STARTS[kind], ...extra], m = +Math.min(per, left()).toFixed(2);
    say(`[회차 ${r}] 씨앗 ${seed}, ${m}분, 시작안 묶음 ${kind}(${starts.length}개)`);
    const { code, text } = await runSearch(m, seed, starts, logFile);
    const ev = /\[탐색 끝\].*?평가 (\d+)개/.exec(text); if (ev) evals += +ev[1];
    const bf = path.join(OUT, `search_s${seed}_best.csv`), ef = path.join(OUT, `search_s${seed}_elite.json`);
    if (code !== 0 || !fs.existsSync(bf)) { say(`  회차 ${r}: 결과 파일 없음(종료 코드 ${code})`); rounds.push({ r, seed, kind, minutes: m, pub: null }); save(`회차 ${r} 실패`); continue; }
    const pub = await pubOf(bf);
    fs.copyFileSync(bf, path.join(DST, path.basename(bf))); if (fs.existsSync(ef)) fs.copyFileSync(ef, path.join(DST, path.basename(ef)));
    rounds.push({ r, seed, kind, minutes: m, pub });
    if (pub < best.pub - 1e-9) { best = { pub, seed, csv: fs.readFileSync(bf, 'utf8') }; extra.push(path.join(DST, path.basename(bf))); }
    say(`  회차 ${r} 끝: 공개 ${pub.toFixed(2)} | 이 조각 최저 ${best.pub.toFixed(2)}${best.pub < base - 1e-9 ? ' (제출안보다 낮음)' : ''}`);
    save(`회차 ${r}`);
  }
  save('끝'); say(`[조각 ${shard} 끝] ${rounds.length}회, 평가 ${evals}개, 최저 공개 ${best.pub.toFixed(2)} (씨앗 ${best.seed})`);
})().catch(e => { console.error(e); process.exit(1); });
