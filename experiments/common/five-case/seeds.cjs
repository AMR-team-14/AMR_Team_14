// Team-wide seed plan. Every candidate, from any teammate, is compared on the same seeds.
//   - public block: seeds 11-15 (the graded leaderboard seeds for Cases 2-5)
//   - held-out blocks: 5-seed blocks inside the team range 1-500 (1-5, 6-10, 16-20, ..., 496-500; 11-15 excluded)
//   - pooled 1-500: one evaluation over all 500 seeds, the same number teammates record with
//     `node simulator/score.cjs <case> <answer> 1 500`
// Stages are nested (screen ⊂ confirm ⊂ final), so a candidate promoted to the next stage reuses its earlier blocks.
'use strict';
const PUBLIC = 11;
const HELD = [];
for (let s = 1; s <= 496; s += 5) if (s !== PUBLIC) HELD.push(s);        // 99 blocks
const blocks = n => {                                                      // first n held-out blocks; past 500 if n > 99
  const out = HELD.slice(0, n);
  for (let s = 501; out.length < n; s += 5) out.push(s);
  return out;
};
const STAGES = {screen: blocks(4), confirm: blocks(40), final: blocks(99)};
const POOLED = {start: 1, count: 500};
module.exports = {PUBLIC, HELD, blocks, STAGES, POOLED};
