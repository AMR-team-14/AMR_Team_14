// Best answers per case for the jsm README: dashboard (public seeds 11-15) and robustness (pooled seeds 1-500).
//   node readme-board.cjs        rewrites the block between the BOARD markers in README.md and best/caseN-*.csv
// Reads team/scores.json (every team answer re-scored with simulator/score.cjs). Deterministic: the files change only
// when a best answer changes, so the analysis commit touches README.md only when a record moves.
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const REPO = path.resolve(__dirname, '..', '..', '..');
const README = path.join(REPO, 'README.md');
const BEST = path.join(REPO, 'best');
const A = Object.values(JSON.parse(fs.readFileSync(path.join(__dirname, 'team', 'scores.json'), 'utf8')).answers);
const START = '<!-- BOARD:START (자동 갱신: experiments/common/five-case/readme-board.cjs) -->', END = '<!-- BOARD:END -->';

const f4 = v => v === null || v === undefined ? '–' : Number(v).toFixed(4);
const owners = a => [...new Set(a.sources.map(s => s.author))].join(', ');
const origin = a => {
  const s = a.sources.find(x => x.kind !== 'search') || a.sources[0];
  if (s.kind === 'search') return `jsm 자동 탐색 \`${(s.path.split('#')[1] || '').split(':').slice(0, 1)}\``;
  return `\`${s.branch}\` ${path.posix.basename(s.path) === 'answer.csv' ? s.path.split('/').slice(-2).join('/') : path.posix.basename(s.path)}`;
};
fs.mkdirSync(BEST, {recursive: true});
const save = (a, name) => { fs.writeFileSync(path.join(BEST, name), a.text); return `[${name}](best/${name})`; };

const LB = (() => { try { return JSON.parse(fs.readFileSync(path.join(__dirname, 'team', 'leaderboard.json'), 'utf8')).records; } catch { return {}; } })();
const rows = [];
for (const c of [1, 2, 3, 4, 5]) {
  const ok = A.filter(a => a.case === c && a.public && a.public.cost !== null);
  const s5 = a => (a.s500 && a.s500.cost !== null ? a.s500.cost : Infinity);
  const pub = [...ok].sort((x, y) => x.public.cost - y.public.cost || s5(x) - s5(y) || x.sha12.localeCompare(y.sha12))[0];
  const rob = ok.filter(a => a.s500 && a.s500.cost !== null && a.s500.rejected === 0)
    .sort((x, y) => x.s500.cost - y.s500.cost || x.sha12.localeCompare(y.sha12))[0];
  // answers with the same score (to 4 decimals) on both objectives are the same record: credit everyone who has one
  const key = a => `${f4(a.public.cost)}|${f4(a.s500 && a.s500.cost)}`;
  const tie = a => ok.filter(x => key(x) === key(a));
  const ownersOf = a => [...new Set(tie(a).flatMap(x => x.sources.map(s => s.author)))].join(', ');
  const rep = a => tie(a).find(x => x.sources.some(s => s.branch !== 'jsm')) || a;
  const cell = (a0, kind) => { const a = a0 && rep(a0); return a ? [`**${f4(kind === 'pub' ? a.public.cost : a.s500.cost)}**`, f4(kind === 'pub' ? (a.s500 && a.s500.cost) : a.public.cost),
    `${a.fleet}대`, ownersOf(a), origin(a), save(a, `case${c}-${kind === 'pub' ? 'dashboard' : 'robust'}.csv`)] : ['–', '–', '–', '–', '–', '–']; };
  const lb = LB[c];
  const lbCell = !lb ? '–' : pub && pub.public.cost < lb.score - 0.005 ? `${lb.score.toFixed(2)} (${lb.who}) → **저장소 답이 기록 경신**`
    : `**${lb.score.toFixed(2)}** (${lb.who})${pub && pub.public.cost > lb.score + 0.005 ? ' — 저장소 답이 아직 못 넘음' : ''}`;
  rows.push({c, pub: cell(pub, 'pub'), rob: cell(rob, 'rob'), lb: lbCell});
}
const lines = [START, '## 최고 기록 (팀 전체 답안, 공통 평가기)', '',
  '모든 브랜치의 답안을 `simulator/score.cjs`로 다시 채점한 값이다. 비용이라 낮을수록 좋다. 기록이 바뀔 때만 이 표와 `best/` 파일이 갱신된다.',
  '- **대시보드**: 공개 시드 11–15 점수 = 리더보드 성적(Case 2–5). Case 1은 비공개 시드로 채점하므로 참고만.',
  '- **범용성(강건성)**: 시드 1–500 묶음 점수, 반려 0인 답만. Case 1은 이 값이 기준.', '',
  '| 케이스 | 리더보드 1위 (기록 보유자) | 저장소 최고 11–15 | 그 답의 1–500 | 대수 | 저장소 답 만든 사람 | 저장소 답 출처 | 답안 |', '|---|---|---|---|---|---|---|---|',
  ...rows.map(r => `| ${r.c} | ${r.lb} | ${r.pub.join(' | ')} |`), '',
  '리더보드 1위 칸만 리더보드 기록이고, 나머지 칸은 저장소에 푸시된 최고 답안에 대한 것이다. 리더보드 기록은 답안 파일이 저장소에 없는 기록까지 포함한다(손으로 적음: `experiments/common/five-case/team/leaderboard.json`). 저장소 최고는 푸시된 답안만 본다.', '',
  '| 케이스 | 범용성 1–500 | 그 답의 11–15 | 대수 | 저장소 답 만든 사람 | 저장소 답 출처 | 답안 |', '|---|---|---|---|---|---|---|',
  ...rows.map(r => `| ${r.c} | ${r.rob.join(' | ')} |`), '',
  '전체 순위와 회차별 분석은 [reports/](experiments/common/five-case/reports/), 인사이트는 [insights/](insights/)에 있다.', END];
const block = lines.join('\n');
let text = fs.readFileSync(README, 'utf8');
text = text.includes(START) ? text.replace(new RegExp(START.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + '[\\s\\S]*?' + END), block)
  : text.replace(/\n## /, '\n' + block + '\n\n## ');
fs.writeFileSync(README, text);
console.log(block);
