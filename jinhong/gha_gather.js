// gha_gather — 분산 탐색 회차(jinhong-search 워크플로, gha_run.js) 결과를 모은다. (gha_collect.js는 Case 1 전용이라 이름을 달리했다.)
//  ① gh run download <실행 id> -D gha/dl_<실행 id>  (이미 있으면 건너뜀. 무시 폴더라 커밋되지 않는다)
//  ② 단계 A 조각의 바닥 목록을 gha/valleys_case<N>.csv 에 누적한다(방향 + 대수로 중복 제거, 더 낮은 점수만 남김). 이 표가 「이미 본 곳」 기록이다.
//  ③ 단계 B 조각의 최저안을 로컬 엔진으로 다시 계산해 점수 순으로 보여 주고, 지금 제출안보다 낮은 것은 case<N>/gha_cand_<실행 id>_<i>.csv 로 쓴다.
//  ④ 상위 안은 pipe.js eval(웹 형식 검사)까지 돌린다. 제출 전에는 safety · 웹 「평가 실행」 대조를 따로 한다.
// 사용: node gha_gather.js <실행 id | 로컬 폴더> [상위 몇 개=5]
const fs = require('fs'), path = require('path'), { spawnSync } = require('child_process');
const { Engine, readSpec, specToCsv, round2 } = require('./gx.js');
const [idArg, TOP = '5'] = process.argv.slice(2), HERE = __dirname, REPO = 'AMR-team-14/AMR_Team_14';
if (!idArg) { console.log('사용: node gha_gather.js <실행 id | 로컬 폴더> [상위 몇 개=5]'); process.exit(1); }
const isDir = fs.existsSync(idArg) && fs.statSync(idArg).isDirectory(), runId = isDir ? path.basename(idArg) : idArg, DL = isDir ? idArg : path.join(HERE, 'gha', 'dl_' + idArg);
if (!isDir && !fs.existsSync(DL)) { const r = spawnSync('gh', ['run', 'download', idArg, '--repo', REPO, '-D', DL], { stdio: 'inherit' }); if (r.status !== 0) console.log('(내려받기에서 오류가 났다. 받은 것만으로 계속한다)'); }
const files = []; (function walk(d) { for (const f of fs.readdirSync(d)) { const p = path.join(d, f); fs.statSync(p).isDirectory() ? walk(p) : files.push(p); } })(DL);
const shards = files.filter(f => path.basename(f) === 'shard.json').map(f => ({ dir: path.dirname(f), ...JSON.parse(fs.readFileSync(f, 'utf8')) }));
console.log(`[실행 ${runId}] 조각 ${shards.length}개: ` + ['A', 'B'].map(st => `단계 ${st} ${shards.filter(s => s.stage === st).length}`).join(' · '));
const hash = d => { let h = 0; for (const c of d) h = (h * 31 + c.charCodeAt(0)) >>> 0; return h.toString(36); };
(async () => {
  for (const CN of [...new Set(shards.map(s => s.case))].sort()) {
    const eng = new Engine(String(CN)), ansF = path.join(HERE, `case${CN}`, `answer_case${CN}.csv`), ans = (await eng.pub([readSpec(ansF)]))[0];
    console.log(`\n== Case ${CN} (지금 제출안 ${ans.toFixed(2)}) ==`);
    // 단계 A: 바닥 표 누적
    const A = shards.filter(s => s.case === CN && s.stage === 'A');
    if (A.length) {
      const vf = path.join(HERE, 'gha', `valleys_case${CN}.csv`), V = new Map();
      if (fs.existsSync(vf)) for (const l of fs.readFileSync(vf, 'utf8').trim().split(/\r?\n/).slice(1)) { const [c, n, pub, dir, nodes, run, sh, seed] = l.split(','); V.set(dir + '|' + n, { n: +n, pub: +pub, dir, nodes: nodes.replace(/\//g, ','), run, sh, seed }); }
      const before = V.size; let seen = 0;
      for (const s of A) for (const f of fs.readdirSync(s.dir).filter(x => /^ilsc?_n\d+_s\d+\.json$/.test(x))) { const m = /_n(\d+)_s(\d+)\.json$/.exec(f), j = JSON.parse(fs.readFileSync(path.join(s.dir, f), 'utf8')); for (const c of j.cands) { seen++; const k = c.dir + '|' + m[1], o = V.get(k); if (!o || c.pub < o.pub - 1e-9) V.set(k, { n: +m[1], pub: c.pub, dir: c.dir, nodes: c.nodes, run: runId, sh: s.shard, seed: m[2] }); } }
      const rows = [...V.values()].sort((a, b) => a.pub - b.pub);
      fs.writeFileSync(vf, 'case,n,pub,dir,nodes,run,shard,seed\n' + rows.map(r => [CN, r.n, r.pub.toFixed(4), r.dir, r.nodes.replace(/,/g, '/'), r.run, r.sh, r.seed].join(',')).join('\n') + '\n');   // nodes의 쉼표는 /로 바꿔 적는다
      console.log(`단계 A: 조각 ${A.length}개의 바닥 ${seen}개 → 표 ${before} → ${V.size}개 (gha/valleys_case${CN}.csv)`);
      for (const n of [...new Set(rows.map(r => r.n))].sort((a, b) => a - b)) { const rn = rows.filter(r => r.n === n); console.log(`  ${n}대 상위: ` + rn.slice(0, 6).map(r => `${r.pub.toFixed(2)}[${hash(r.dir)}]`).join(' ') + ` (바닥 ${rn.length}개)`); }
      // 상위 바닥을 로컬에서 다시 계산(분해 포함) — 서로 다른 방향만
      const top = []; for (const r of rows) { if (top.every(t => t.dir !== r.dir)) top.push(r); if (top.length >= +TOP) break; }
      const re = await eng.pub(top.map(r => ({ dir: r.dir, n: r.n, nodes: r.nodes })));
      top.forEach((r, i) => { const f = path.join(HERE, `case${CN}`, `gha_valley_${runId}_${i + 1}.csv`); fs.writeFileSync(f, specToCsv({ dir: r.dir, n: r.n, nodes: r.nodes })); console.log(`  ${i + 1}. ${r.pub.toFixed(2)} (로컬 재계산 ${re[i].toFixed(2)}) ${r.n}대 | 조각 ${r.sh} 씨앗 ${r.seed} | ${r.dir} → ${path.basename(f)}`); });
    }
    // 단계 B: 조각별 최저안
    const B = shards.filter(s => s.case === CN && s.stage === 'B');
    if (B.length) {
      const cand = []; for (const s of B) { const bf = path.join(s.dir, 'best.csv'), fall = fs.readdirSync(s.dir).filter(x => /^best_\d+\.csv$/.test(x)).sort((a, b) => parseInt(b.slice(5)) - parseInt(a.slice(5)))[0]; const f = fs.existsSync(bf) ? bf : fall ? path.join(s.dir, fall) : null; if (f) cand.push({ s, f, spec: readSpec(f) }); }
      const re = await eng.pub(cand.map(c => c.spec)); cand.forEach((c, i) => c.pub = re[i]); cand.sort((a, b) => a.pub - b.pub);
      console.log(`단계 B: 조각 ${B.length}개 중 결과 ${cand.length}개`);
      cand.forEach((c, i) => console.log(`  ${i + 1}. ${Number.isFinite(c.pub) ? c.pub.toFixed(3) : '반려'} | 조각 ${c.s.shard} (출발 ${c.s.start ? c.s.start.toFixed(2) : '?'}${c.s.startNote ? ', ' + c.s.startNote : ''}) ${c.spec.n}대 기준 ${c.spec.nodes.split(';').length}개`));
      const better = cand.filter(c => c.pub < ans - 1e-9).slice(0, +TOP);
      better.forEach((c, i) => { const f = path.join(HERE, `case${CN}`, `gha_cand_${runId}_${i + 1}.csv`); fs.copyFileSync(c.f, f); console.log(`  → 제출안보다 낮음: ${c.pub.toFixed(3)} ${path.relative(HERE, f)}`); spawnSync(process.execPath, ['pipe.js', String(CN), 'eval', f], { cwd: HERE, stdio: 'inherit' }); });
      if (!better.length) console.log('  제출안보다 낮은 안이 없다.');
    }
    eng.close();
  }
})().catch(e => { console.error(e); process.exit(1); });
