// runcase — Case N 파이프라인(PIPELINE.md 1절)을 처음부터 끝까지 자동으로 돌린다. 결과는 caseN/run.log 와 caseN/answer_caseN.csv.
// 사용: node runcase.js <N> [스레드=4] [1차 탐색 분=12] [2차 탐색 분=12]
// 여러 케이스를 동시에 돌릴 때는 스레드 합이 코어 수를 넘지 않게 한다. 제출은 하지 않는다.
const { spawnSync, spawn } = require('child_process');
const fs = require('fs'), path = require('path');
const [N, TH = '4', M1 = '12', M2 = '12'] = process.argv.slice(2);
const OUT = path.join(__dirname, 'case' + N); fs.mkdirSync(OUT, { recursive: true });
const logf = path.join(OUT, 'run.log'), t0 = Date.now();
const log = s => { const l = `[${((Date.now() - t0) / 60000).toFixed(1)}분] ${s}`; console.log(l); fs.appendFileSync(logf, l + '\n'); };
const env = th => ({ ...process.env, AGVX_THREADS: String(th) });
function run(cmd, args, th = TH) { const r = spawnSync(cmd, args, { cwd: __dirname, env: env(th), encoding: 'utf8', maxBuffer: 1 << 26 }); return (r.stdout || '') + (r.stderr || ''); }
const pipe = (...a) => run('node', ['pipe.js', N, ...a]);
const lines = txt => txt.trim().split(/\r?\n/);
const last = (txt, n = 1) => lines(txt).slice(-n).join('\n');
const C = f => path.join('case' + N, f), has = f => fs.existsSync(path.join(OUT, f));
const dirOf = f => (fs.readFileSync(path.join(__dirname, f), 'utf8').match(/^dir,(\d+)/m) || [])[1];
function searchPair(min, seeds, starts) {   // 씨앗 두 개를 동시에(스레드를 반씩)
  const th = Math.max(1, Math.floor(+TH / 2));
  return Promise.all(seeds.map(sd => new Promise(res => {
    const p = spawn('node', ['pipe.js', N, 'search', String(min), String(sd), ...starts], { cwd: __dirname, env: env(th) });
    let o = ''; p.stdout.on('data', d => o += d); p.stderr.on('data', d => o += d); p.on('close', () => res(o));
  })));
}
function evalAll(files) {     // 웹 형식 검사를 통과한 것만, 공개 점수 오름차순
  if (!files.length) return [];
  const out = pipe('eval', ...files), res = [];
  for (const l of lines(out)) { const m = l.match(/^(\S+\.csv): 공개 ([\d.]+)/); if (m) res.push({ f: files.find(x => path.basename(x) === m[1]), pub: +m[2], line: l }); }
  return res.sort((a, b) => a.pub - b.pub);
}
(async () => {
  log(`=== Case ${N} 시작 (스레드 ${TH}, 탐색 ${M1}+${M2}분) ===`);
  const v = last(run('node', ['gx_verify.js', N, '60'])); log('1 엔진 대조: ' + v);
  if (!/다른 것 0건/.test(v)) { log('엔진이 웹 원본과 다르다 → 중단'); process.exit(1); }
  log('2 기준선:\n' + last(pipe('base'), 4));
  log('3 방향 MIP 1차: ' + lines(run('python', ['-I', 'gurobi/dir_mip.py', N, C('dirs.json'), '60', '0.10'])).filter(l => /후보|z0:/.test(l)).join(' | '));
  log('3 방향 MIP 2차: ' + lines(run('python', ['-I', 'gurobi/dir_mip.py', N, C('dirs_ext.json'), '30', '0.06', 'ext'])).filter(l => /후보/.test(l)).join(' | '));
  log('4 방향 순위 1차:\n' + lines(pipe('dirrank', '10')).filter(l => /^\s+[1-5]\. |z0:/.test(l)).join('\n'));
  log('4 방향 순위 2차:\n' + lines(pipe('dirrank', '6', 'dirs_ext.json')).filter(l => /^\s+[1-3]\. /.test(l)).join('\n'));
  // 5 구조 훑기: 서로 다른 방향 3개(1차 상위 + 2차 상위)
  const structs = [], doneDir = new Set();
  const doStruct = f => {
    const d = dirOf(f); if (!d || doneDir.has(d)) return; doneDir.add(d);
    log(`  구조 훑기 ${f}: ` + last(pipe('struct', f, '3')));
    const o = C(`struct_${path.basename(f, '.csv')}.csv`); if (fs.existsSync(path.join(__dirname, o))) structs.push(o);
  };
  log('5 구조 훑기(방향 3개)');
  for (const f of ['dirtop_1.csv', 'dirtop_ext_1.csv', 'dirtop_2.csv', 'dirtop_ext_2.csv', 'dirtop_3.csv'].filter(has).map(C)) { if (doneDir.size >= 3) break; doStruct(f); }
  // 6 1차 탐색
  const starts1 = [...structs, ...['dirtop_1.csv', 'dirtop_2.csv', 'dirtop_3.csv', 'dirtop_4.csv', 'dirtop_ext_1.csv'].filter(has).map(C)];
  log(`6 1차 탐색(씨앗 1 · 2, ${M1}분)`);
  (await searchPair(M1, [1, 2], starts1)).forEach(o => log('  ' + lines(o).filter(l => /1세대 점검|탐색 끝/.test(l)).join('\n  ')));
  // 7 탐색 최저안의 방향에서 구조 훑기 + 방향 옮겨 보기
  const sfiles = () => ['search_s1_best.csv', 'search_s2_best.csv', 'search_s5_best.csv', 'search_s6_best.csv'].filter(has).map(C);
  let cand = evalAll([...structs, ...sfiles()]);
  log('7 중간 순위: ' + cand.slice(0, 3).map(c => `${c.pub} ${path.basename(c.f)}`).join(' | '));
  if (cand.length) doStruct(cand[0].f);
  cand = evalAll([...structs, ...sfiles()]);
  for (const src of ['dirs_ext.json', 'dirs.json']) {
    log(`  옮겨 보기 ${src}:\n` + lines(pipe('transfer', src, ...cand.slice(0, 2).map(c => c.f))).filter(l => /^\s+[1-3]\. /.test(l)).join('\n'));
    if (has('xfer_1.csv')) { const keep = C(`xfer_${src.replace('.json', '')}_1.csv`); fs.copyFileSync(path.join(OUT, 'xfer_1.csv'), path.join(__dirname, keep)); doStruct(keep); }
  }
  // 8 2차 탐색
  cand = evalAll([...structs, ...sfiles()]);
  log(`8 2차 탐색(씨앗 5 · 6, ${M2}분) 시작안: ` + cand.slice(0, 6).map(c => `${c.pub}`).join(' '));
  (await searchPair(M2, [5, 6], cand.slice(0, 6).map(c => c.f))).forEach(o => log('  ' + lines(o).filter(l => /1세대 점검|탐색 끝/.test(l)).join('\n  ')));
  // 9 최종: 웹 형식 검사를 통과한 것 중 공개 최저 → answer, 새 시드 1,000개
  cand = evalAll([...structs, ...sfiles()]);
  if (!cand.length) { log('유효한 후보가 없다'); process.exit(1); }
  const nk = f => (fs.readFileSync(path.join(__dirname, f), 'utf8').match(/^key,/gm) || []).length;   // 점수가 같으면 기준 수가 적은 안
  const best = cand.filter(c => c.pub <= cand[0].pub + 1e-9).sort((a, b) => nk(a.f) - nk(b.f))[0], ans = C(`answer_case${N}.csv`);
  fs.copyFileSync(path.join(__dirname, best.f), path.join(__dirname, ans));
  log('9 최종 순위:\n  ' + cand.slice(0, 5).map(c => c.line).join('\n  '));
  log(`  → ${ans} (${path.basename(best.f)})\n` + fs.readFileSync(path.join(__dirname, ans), 'utf8').trim());
  log('  ' + last(pipe('safety', ans, '900001', '1000')));
  for (const b of ['base_FCFS.csv', 'base_STTF.csv', 'base_URG.csv'].filter(has)) log('  기준선 ' + last(pipe('safety', C(b), '900001', '1000')));
  log('  배차 진단:\n' + last(run('node', ['gdiag.js', N, ans]), 4));
  log(`=== Case ${N} 끝 ===`);
})();
