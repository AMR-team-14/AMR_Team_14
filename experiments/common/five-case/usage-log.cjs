// Record how much the AI step used, from `claude -p --output-format json`.
//   node usage-log.cjs <claude-log.json>   append one row to reports/usage.jsonl and print it
//   node usage-log.cjs --last-line         print the last row as a report line (markdown)
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const LOG = path.join(__dirname, 'reports', 'usage.jsonl');
const arg = process.argv[2];
if (arg === '--last-line') {
  if (!fs.existsSync(LOG)) process.exit(0);
  const rows = fs.readFileSync(LOG, 'utf8').trim().split('\n');
  const u = JSON.parse(rows[rows.length - 1]);
  if (u.run !== process.env.RUN) process.exit(0);
  console.log(`\n> AI 사용량: 모델 ${u.models.join(', ')}, ${u.turns}턴, ${u.seconds}초, 입력 ${u.input} + 캐시 읽기 ${u.cacheRead} + 캐시 쓰기 ${u.cacheWrite}, 출력 ${u.output} 토큰 (API 요금 환산 $${u.apiEquivalentUSD})`);
  process.exit(0);
}
const r = JSON.parse(fs.readFileSync(arg, 'utf8'));
const u = r.usage || {};
const row = {at: new Date().toISOString(), run: process.env.RUN || null, models: Object.keys(r.modelUsage || {}), turns: r.num_turns,
  seconds: Math.round((r.duration_ms || 0) / 1000), input: u.input_tokens, output: u.output_tokens,
  cacheRead: u.cache_read_input_tokens, cacheWrite: u.cache_creation_input_tokens,
  apiEquivalentUSD: r.total_cost_usd === undefined ? null : Math.round(r.total_cost_usd * 1000) / 1000, isError: !!r.is_error};
fs.mkdirSync(path.dirname(LOG), {recursive: true});
fs.appendFileSync(LOG, JSON.stringify(row) + '\n');
console.log(JSON.stringify(row));
