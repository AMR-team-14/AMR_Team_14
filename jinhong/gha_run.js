// gha_run — 분산 탐색의 조각 하나(GitHub Actions 러너 · Kaggle 노트북 · 로컬 어디서나 같은 스크립트).
//  회차 명세 gha/round.json(환경변수 GHA_ROUND로 다른 파일 지정 가능)을 읽고, 내 조각 번호가 맡은 일을 한다.
//    { "round": "r1", "minutes": 270, "jobs": [ { "case": 4, "stage": "A", "shards": 12, ... }, { "case": 5, "stage": "B", "shards": 8, ... } ] }
//  조각 번호는 jobs를 앞에서부터 shards만큼씩 차지한다(0~11 → 첫 일, 12~19 → 둘째 일). 남는 번호는 바로 끝난다.
//  단계 A(넓게): 출발 방향 묶음(starts의 cands[].dir 중 내 몫)에서 ils.js(순환 뒤집기 + 큰 차기)로 대수별 바닥 목록을 만든다.
//  단계 B(깊게): 바닥 하나(starts의 cands[조각] = {dir, n, nodes})를 유전 탐색 → 나무 이웃(tt) → 대기 가지 재전수(wenum) → 나무 이웃으로 번갈아 다듬는다.
//  시작 전에 expect.file이 expect.pub으로 재현되는지 확인하고, 다르면 멈춘다(엔진 · 케이스 데이터가 다르다는 뜻).
//  깃허브 전용 값에 기대지 않는다: 조각 번호(인자 또는 GHA_SHARD), 케이스 JSON 폴더(AGV_CASES), 스레드 수(AGVX_THREADS)만 받는다.
//  결과는 gha/out/<회차>/case<N>_<단계>_shard<번호>/ 에 모은다(워크플로가 아티팩트로 올린다. 리포에는 안 올라간다).
// 사용: node gha_run.js <조각 번호>
const fs = require('fs'), path = require('path'), { spawnSync } = require('child_process');
const { Engine, readSpec, specToCsv, round2 } = require('./gx.js');
const HERE = __dirname, R = JSON.parse(fs.readFileSync(path.join(HERE, process.env.GHA_ROUND || 'gha/round.json'), 'utf8'));
let shard = +(process.argv[2] ?? process.env.GHA_SHARD ?? 0), job = null, s = shard;
for (const j of R.jobs) { if (s < j.shards) { job = j; break; } s -= j.shards; }
if (!job) { console.log(`[조각 ${shard}] 회차 ${R.round}에 맡은 일이 없다.`); process.exit(0); }
const CN = String(job.case), OUT = path.join(HERE, 'case' + CN), DST = path.join(HERE, 'gha', 'out', R.round, `case${CN}_${job.stage}_shard${s}`);
fs.mkdirSync(DST, { recursive: true });
const t0 = Date.now(), left = () => R.minutes - (Date.now() - t0) / 60000, logF = path.join(DST, 'shard.log');
const say = x => { console.log(x); fs.appendFileSync(logF, x + '\n'); };
const pubOf = async file => { const eng = new Engine(CN); const v = (await eng.pub([readSpec(file)]))[0]; eng.close(); return v; };
// 도구 하나를 끝까지(또는 제한 시간까지) 돌린다. 출력은 러너 로그로 바로 흘려보낸다.
function run(script, args, env = {}, limitMin = 60) {
  const r = spawnSync(process.execPath, [script, ...args.map(String)], { cwd: HERE, env: { ...process.env, ...env }, stdio: 'inherit', timeout: Math.round(Math.max(1, limitMin) * 60000), killSignal: 'SIGKILL' });   // timeout은 정수여야 한다(소수면 RangeError — r1 첫 시도에서 Case 4 조각이 여기서 멈췄다)
  if (r.error) say(`  (${script} ${args[1] || ''}: ${r.error.code === 'ETIMEDOUT' ? `제한 ${limitMin.toFixed(0)}분에 끊음` : r.error.message})`);
  return r.status === 0;
}
const keep = (f, as) => { if (fs.existsSync(f)) fs.copyFileSync(f, path.join(DST, as || path.basename(f))); };
(async () => {
  const exp = path.join(HERE, job.expect.file), base = await pubOf(exp);
  say(`[회차 ${R.round} · Case ${CN} · 단계 ${job.stage} · 조각 ${s}/${job.shards}] 제출안 ${base.toFixed(4)} (기대 ${job.expect.pub}), ${R.minutes}분, 스레드 ${process.env.AGVX_THREADS || '기본'}`);
  if (round2(base) !== job.expect.pub) { say('제출안 점수가 기대값과 다르다: 엔진 빌드나 케이스 데이터를 확인. 멈춘다.'); process.exit(1); }
  const summary = { round: R.round, case: +CN, stage: job.stage, shard: s, expect: job.expect.pub, steps: [] };
  const done = () => { summary.minutes = +((Date.now() - t0) / 60000).toFixed(1); fs.writeFileSync(path.join(DST, 'shard.json'), JSON.stringify(summary, null, 1)); };
  if (job.stage === 'A') {
    // 넓게: 내 몫의 출발 방향에서 대수마다 ils.js
    const all = JSON.parse(fs.readFileSync(path.join(HERE, job.starts), 'utf8')).cands.map(c => c.dir), mine = all.filter((_, i) => i % job.shards === s);
    fs.writeFileSync(path.join(OUT, `gha_pool_${s}.json`), JSON.stringify({ cands: mine.map(dir => ({ dir })) }));
    const per = (R.minutes - 4) / job.ns.length;
    for (const [ni, n] of job.ns.entries()) {
      const seed = job.seed0 + s * 10 + ni, m = Math.min(per, left() - 1); if (m < 2) break;
      say(`  [${(R.minutes - left()).toFixed(0)}분] ${n}대, 출발 방향 ${mine.length}개 뒤 큰 차기, ${m.toFixed(0)}분, 씨앗 ${seed}`);
      run('ils.js', [CN, exp, n, m.toFixed(1), seed, job.kh || 4, job.rs || '75,90,105'], { ILS_CYC: '1', ILS_KMIN: String(job.kmin || 4), ILS_KMAX: String(job.kmax || 9), ILS_POOL: `gha_pool_${s}.json` }, m * 1.25 + 5);
      const jf = path.join(OUT, `ilsc_n${n}_s${seed}.json`); keep(jf);
      if (fs.existsSync(jf)) { const top = JSON.parse(fs.readFileSync(jf, 'utf8')).cands.slice(0, 3); summary.steps.push({ n, seed, minima: JSON.parse(fs.readFileSync(jf, 'utf8')).cands.length, top: top.map(c => +c.pub.toFixed(3)) }); say(`    ${n}대 바닥 상위: ${top.map(c => c.pub.toFixed(2)).join(' ')}`); }
      done();
    }
    keep(path.join(OUT, 'ils.log'));
  } else {
    // 깊게: 바닥 하나를 유전 탐색 ↔ 나무 이웃 ↔ 대기 가지 재전수로 다듬는다
    const vs = JSON.parse(fs.readFileSync(path.join(HERE, job.starts), 'utf8')).cands, v = vs[s % vs.length], seed0 = job.seed0 + s * 50 + Math.floor(s / vs.length) * 7;
    let bestF = path.join(DST, 'start.csv'); fs.writeFileSync(bestF, specToCsv({ dir: v.dir, n: v.n, nodes: v.nodes }));
    let best = await pubOf(bestF), step = 0, didW = false; summary.start = best; summary.startNote = v.note || '';
    say(`  출발 ${best.toFixed(3)} (${v.n}대${v.note ? ', ' + v.note : ''})`);
    const take = async (f, what) => { if (!fs.existsSync(f)) return; const p = await pubOf(f); summary.steps.push({ what, pub: Number.isFinite(p) ? +p.toFixed(4) : null, at: +(R.minutes - left()).toFixed(1) }); if (p < best - 1e-9) { best = p; bestF = path.join(DST, `best_${++step}.csv`); fs.copyFileSync(f, bestF); say(`    ${what}: ${p.toFixed(3)} ← 새 최저`); } else say(`    ${what}: ${Number.isFinite(p) ? p.toFixed(3) : '반려'} (최저 ${best.toFixed(3)})`); summary.best = best; done(); };
    const gaEnv = { ...(job.nofleet === false ? {} : { PIPE_NOFLEET: '1' }), ...(job.nodir ? { PIPE_NODIR: '1' } : {}) };
    for (let cyc = 0; left() > 6; cyc++) {
      // ① 유전 탐색
      const seed = seed0 + cyc, m = Math.min(job.ga || 20, left() - 3); if (m < 2) break;
      fs.rmSync(path.join(OUT, `search_g${s}_s${seed}_best.csv`), { force: true });
      run('pipe.js', [CN, 'search', m.toFixed(1), seed, bestF], { ...gaEnv, PIPE_TAG: `g${s}_` }, m + 6);
      await take(path.join(OUT, `search_g${s}_s${seed}_best.csv`), `유전 탐색(씨앗 ${seed}, ${m.toFixed(0)}분)`);
      // ② 나무 이웃
      if (left() > 8) { fs.rmSync(path.join(OUT, 'nt_tt_1.csv'), { force: true }); run('night.js', [CN, 'tt', bestF, 2, job.ttK || 30, 3], {}, Math.min(job.ttMin || 30, left() - 2)); await take(path.join(OUT, 'nt_tt_1.csv'), '나무 이웃'); }
      // ③ 대기 가지 재전수(루트가 w일 때, 한 번만) → 그 상위에서 나무 이웃
      if (!didW && left() > 25 && /^key,1,w,/m.test(fs.readFileSync(bestF, 'utf8'))) {
        didW = true; for (let i = 1; i <= 3; i++) fs.rmSync(path.join(OUT, `wenum_${i}.csv`), { force: true });
        run('wenum.js', [CN, bestF], { ...(job.wenumDe ? { WENUM_DE: String(job.wenumDe) } : {}), ...(job.wenumNoQo ? { WENUM_NOQO: '1' } : {}) }, Math.min(job.wenumMin || 35, left() - 12));
        for (let i = 1; i <= 2 && left() > 8; i++) { const wf = path.join(OUT, `wenum_${i}.csv`); if (!fs.existsSync(wf)) break; await take(wf, `대기 가지 재전수 ${i}위`); fs.rmSync(path.join(OUT, 'nt_tt_1.csv'), { force: true }); run('night.js', [CN, 'tt', wf, 2, job.ttK || 30, 3], {}, Math.min(job.ttMin || 30, left() - 2)); await take(path.join(OUT, 'nt_tt_1.csv'), `재전수 ${i}위에서 나무 이웃`); }
      }
    }
    fs.copyFileSync(bestF, path.join(DST, 'best.csv'));
    say(`[조각 ${s} 끝] 출발 ${summary.start.toFixed(3)} → 최저 ${best.toFixed(3)}`);
  }
  done();
})().catch(e => { console.error(e); process.exit(1); });
