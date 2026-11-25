// 실험 한 건을 팀 공통 양식(run.json · answer.csv · results.csv · logs/)으로 남긴다.
// 저장소 루트에서:
//   node experiments/record.cjs <케이스> <답안 경로> <실험ID> "<실행 목적>" [--src <원본 커밋>] [--search "<탐색에 쓴 시드>"] [--note "<메모>"] [--quick]
// 공통 평가기(simulator/score.cjs)로 공개 확인 시드 11~15와 최종 비교 시드 1~500을 --trace로 돌린다.
// --quick 이면 11~15만 돌린다(중간 실험용). 같은 실험ID 폴더가 있으면 멈춘다(중복 방지).
const fs = require('node:fs'), path = require('node:path');
const { execFileSync } = require('node:child_process');
const { createHash } = require('node:crypto');
const root = path.resolve(__dirname, '..');
const args = process.argv.slice(2), opt = {}, pos = [];
for (let i = 0; i < args.length; i++) {
  if (args[i] === '--quick') opt.quick = true;
  else if (args[i].startsWith('--')) opt[args[i].slice(2)] = args[++i];
  else pos.push(args[i]);
}
const [caseArg, answerPath, id, purpose] = pos;
if (!/^[1-5]$/.test(caseArg || '') || !answerPath || !id || !purpose) {
  console.error('사용: node experiments/record.cjs <케이스 1~5> <답안 경로> <실험ID> "<실행 목적>" [--src 커밋] [--search "시드"] [--note "메모"] [--quick]');
  process.exit(1);
}
const dir = path.join(root, 'experiments', `case${caseArg}`, id);
if (fs.existsSync(dir)) { console.error(`이미 있는 실험ID: ${path.relative(root, dir)} — 기존 결과를 재사용하거나 다른 ID를 쓴다.`); process.exit(1); }
const csv = fs.readFileSync(answerPath, 'utf8').replace(/\r\n/g, '\n').trim() + '\n';
const rows = csv.trim().split('\n');
const inputs = require(path.join(root, 'simulator', 'internal', 'inputs.json'));
const git = (...a) => { try { return execFileSync('git', a, { cwd: root, encoding: 'utf8' }).trim(); } catch { return '미확인'; } };
fs.mkdirSync(path.join(dir, 'logs'), { recursive: true });
fs.writeFileSync(path.join(dir, 'answer.csv'), csv);
const rel = path.relative(root, path.join(dir, 'answer.csv')).replace(/\\/g, '/');
const ranges = opt.quick ? [[11, 5]] : [[11, 5], [1, 500]];
const runs = ranges.map(([first, count]) => {
  const t0 = Date.now();
  const out = execFileSync('node', ['simulator/score.cjs', caseArg, rel, String(first), String(count), '--trace'], { cwd: root, encoding: 'utf8' }).trim();
  fs.writeFileSync(path.join(dir, 'logs', `score_${first}_${count}.json`), out + '\n');
  const r = JSON.parse(out);
  console.log(`시드 ${first}~${first + count - 1}: ${r.pooled_cost === null ? '반려 ' + r.rejected.join(' ') : r.pooled_cost} (${((Date.now() - t0) / 1000).toFixed(0)}초)`);
  return r;
});
fs.writeFileSync(path.join(dir, 'results.csv'),
  'case,fleet,first_seed,seeds,pooled_cost,rejected_seeds,dispatch_count,dispatch_sha256\n' +
  runs.map(r => [r.case, r.fleet, r.first_seed, r.seeds, r.pooled_cost ?? '', r.rejected.join(' '), r.dispatch_count, r.dispatch_sha256].join(',')).join('\n') + '\n');
const kst = new Date(Date.now() + 9 * 3600e3).toISOString().replace('T', ' ').slice(0, 16);
fs.writeFileSync(path.join(dir, 'run.json'), JSON.stringify({
  experiment_id: id,
  case: +caseArg,
  purpose,
  recorded_at_kst: kst,
  answer: { file: 'answer.csv', sha256: createHash('sha256').update(csv).digest('hex'), dir: rows[0].split(',')[1], fleet: +rows[1].split(',')[1], tree_nodes: rows.length - 2 },
  evaluator: { script: 'simulator/score.cjs', commit: git('log', '-1', '--format=%H', '--', 'simulator'), input_path: inputs[caseArg - 1].path ?? '미확인', input_sha: inputs[caseArg - 1].sha ?? '미확인', options: '--trace' },
  aggregation: '시드 묶음의 워밍업 이후 완료 픽업 기록을 합쳐 비용을 한 번 계산(반려 시드가 있으면 비용 칸을 비움)',
  seeds_evaluated: ranges.map(([f, c]) => `${f}~${f + c - 1}`),
  seeds_used_for_search: opt.search ?? '미확인',
  source_commit: opt.src ?? '미확인',
  note: opt.note ?? '',
  results: runs,
  reproduce: ranges.map(([f, c]) => `node simulator/score.cjs ${caseArg} ${rel} ${f} ${c} --trace`),
}, null, 2) + '\n');
console.log(`기록: ${path.relative(root, dir).replace(/\\/g, '/')}/`);
