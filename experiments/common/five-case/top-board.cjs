// insights/TOP.md: the top answers of every case on both objectives, with their trees and the strategy behind them.
//   node top-board.cjs [--top 3]
// Numbers and tree pictures come from code: team/scores.json (every team answer re-scored with simulator/score.cjs)
// and render-trees.cjs (the team tree viewer docs/tree-viewer, rendered to PNG). The strategy text is the
// "상위권 전략" part of each case section in insights/known-insights.md, written by the analysis AI.
// Writes insights/TOP.md, insights/trees/<case>-<sha12>.csv and insights/trees/manifest.json (what to render).
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const L = require('./lib.cjs');

const REPO = path.resolve(__dirname, '..', '..', '..');
const OUT = path.join(REPO, 'insights');
const TREES = path.join(OUT, 'trees');
const arg = (k, d) => { const i = process.argv.indexOf(k); return i > 0 ? process.argv[i + 1] : d; };
const TOP = +arg('--top', '3');
const A = Object.values(JSON.parse(fs.readFileSync(path.join(__dirname, 'team', 'scores.json'), 'utf8')).answers);
const LB = (() => { try { return JSON.parse(fs.readFileSync(path.join(__dirname, 'team', 'leaderboard.json'), 'utf8')).records; } catch { return {}; } })();
const known = (() => { try { return fs.readFileSync(path.join(OUT, 'known-insights.md'), 'utf8'); } catch { return ''; } })();
fs.mkdirSync(TREES, {recursive: true});

const f4 = v => v === null || v === undefined ? '–' : Number(v).toFixed(4);
const s5 = a => (a.s500 && a.s500.cost !== null ? a.s500.cost : Infinity);
const who = a => [...new Set(a.sources.map(s => s.author))].join(', ');
const from = a => {
  const s = a.sources.find(x => x.kind !== 'search') || a.sources[0];
  return s.kind === 'search' ? `jsm 자동 탐색 ${(s.path.split('#')[1] || '').split(':')[0]}` : `${s.branch}: ${s.path}`;
};
const design = a => L.parseDesign(a.text);
const dirIds = {};
const dirId = (c, dir) => { const m = dirIds[c] || (dirIds[c] = new Map()); if (!m.has(dir)) m.set(dir, 'D' + (m.size + 1)); return m.get(dir); };
// one line per node in reading order, e.g. "1 w max 0.001 → (≤) 2 / (>) 3"
const treeText = a => design(a).nodes.sort((x, y) => x.id - y.id)
  .map(n => `${n.id}. ${n.f} ${n.mx} ${n.th}${n.lo || n.hi ? ` → ≤:${n.lo || '끝'} / >:${n.hi || '끝'}` : ''}`).join('<br>');

// strategy text written by the AI: "#### 상위권 전략" (or a line starting with "상위권 전략") inside "## Case N"
function strategy(c) {
  const sec = known.split(/\n(?=## )/).find(s => new RegExp(`^## Case ${c}\\b`).test(s));
  if (!sec) return null;
  // from the "상위권 전략" heading to the next heading or the next line that is not a bullet / indented / quote
  const m = sec.match(/(?:^|\n)#{0,4}\s*상위권 전략[^\n]*\n([\s\S]*?)(?=\n#{1,4} |\n(?![-*>\s])|$)/);
  return m ? m[1].trim() : null;
}

const manifest = [];
const lines = ['# 상위권 답안 한눈에 보기', '',
  '케이스별로 **대시보드(공개 시드 11–15, 리더보드 성적)** 상위와 **범용성(시드 1–500 묶음, 반려 0)** 상위를 모았다. 비용이라 낮을수록 좋다.',
  '숫자는 팀 전체 답안을 공통 평가기(`simulator/score.cjs`)로 다시 채점한 값, 트리 그림은 팀 트리 뷰어(`docs/tree-viewer`)로 그린 것이다.',
  '"상위권 전략"은 자동 분석 AI가 `insights/known-insights.md`에 쓰는 해석이다. Case 1은 비공개 시드로 채점하므로 범용성이 기준이다.',
  'README의 최고 기록 표와 같은 데이터에서 만든다. 표 위 순위는 같은 점수를 하나로 묶었다.', ''];
for (const c of [1, 2, 3, 4, 5]) {
  const ok = A.filter(a => a.case === c && a.public && a.public.cost !== null);
  const uniq = (list, key) => { const seen = new Set(); return list.filter(a => { const k = key(a); if (seen.has(k)) return false; seen.add(k); return true; }); };
  const dash = uniq([...ok].sort((x, y) => x.public.cost - y.public.cost || s5(x) - s5(y)), a => f4(a.public.cost) + '|' + f4(s5(a))).slice(0, TOP);
  const rob = uniq(ok.filter(a => a.s500 && a.s500.cost !== null && a.s500.rejected === 0).sort((x, y) => x.s500.cost - y.s500.cost || x.public.cost - y.public.cost),
    a => f4(s5(a)) + '|' + f4(a.public.cost)).slice(0, TOP);
  const lb = LB[c];
  lines.push(`## Case ${c}`, '');
  if (lb) lines.push(`리더보드 1위 기록: **${lb.score.toFixed(2)}** (${lb.who}, ${lb.source})`, '');
  const block = (title, list, main) => {
    lines.push(`### ${title}`, '', '| 순위 | 점수 | 다른 목표 점수 | 대수 | 통로 | 노드 | 만든 사람 | 출처 |', '|---|---|---|---|---|---|---|---|');
    list.forEach((a, i) => lines.push(`| ${i + 1} | **${f4(main === 'pub' ? a.public.cost : a.s500.cost)}** | ${main === 'pub' ? '1–500 ' + f4(s5(a)) : '11–15 ' + f4(a.public.cost)} | ${a.fleet} | ${dirId(c, design(a).dir)} | ${design(a).nodes.length} | ${who(a)} | ${from(a)} |`));
    lines.push('');
    list.forEach((a, i) => {
      const csv = `case${c}-${a.sha12}.csv`, png = `case${c}-${a.sha12}.png`;
      fs.writeFileSync(path.join(TREES, csv), a.text);
      if (!manifest.some(m => m.png === png)) manifest.push({case: c, csv, png, label: `Case ${c} · ${a.sha12} · ${a.fleet}대`});
      lines.push(`<details${i === 0 ? ' open' : ''}><summary>${i + 1}위 ${a.sha12} 트리 (${a.fleet}대, ${dirId(c, design(a).dir)}) · <a href="trees/${csv}">CSV</a></summary>`, '',
        `![Case ${c} ${a.sha12}](trees/${png})`, '', `<sub>${treeText(a)}</sub>`, '', '</details>', '');
    });
  };
  if (c !== 1) block('대시보드 상위 (공개 11–15)', dash, 'pub');
  block(c === 1 ? '범용성 상위 (시드 1–500) — Case 1 채점 기준' : '범용성 상위 (시드 1–500)', rob, 'rob');
  const st = strategy(c);
  lines.push('### 상위권 전략', '', st || '_자동 분석이 아직 이 케이스의 상위권 전략을 쓰지 않았다._', '');
  const legend = [...(dirIds[c] || new Map())].map(([d, id]) => `${id} \`${d}\``).join(' · ');
  if (legend) lines.push(`<sub>통로: ${legend}</sub>`, '');
}
fs.writeFileSync(path.join(OUT, 'TOP.md'), lines.join('\n') + '\n');
fs.writeFileSync(path.join(TREES, 'manifest.json'), JSON.stringify(manifest, null, 1) + '\n');
// remove tree files no longer on the board
const keep = new Set(manifest.flatMap(m => [m.csv, m.png]).concat('manifest.json'));
for (const f of fs.readdirSync(TREES)) if (!keep.has(f)) fs.rmSync(path.join(TREES, f));
console.log(`TOP.md: ${manifest.length} trees`);
