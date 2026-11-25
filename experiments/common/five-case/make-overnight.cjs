const fs = require('node:fs');
const L = require('./lib.cjs');
const batch = 'overnight-1125';
const gh = Array.from({ length: 8 }, () => []);
const kaggle = [];
let seq = 0;
function add(task, env) {
  const target = env === 'github' ? gh[seq++ % gh.length] : kaggle;
  target.push({ ...task, batch, env });
}
const b1 = fs.readFileSync('jobs/B1.jsonl', 'utf8').trim().split(/\r?\n/).map(JSON.parse);
const cells = new Map();
for (const row of b1) {
  if (row.case === 2 && row.type === 'eval' && /^c2-cell[0-7](:|$)/.test(row.label)) cells.set(row.label.split(':')[0], row);
}
if (cells.size !== 8) throw Error('Missing factorial cells: ' + [...cells.keys()]);
for (const [label, row] of cells) {
  const {case: c, candSha, design} = row;
  for (let k = 0; k < 200; k++) {
    const start = 2430001 + 5 * k;
    add({id:batch + ':' + label + ':' + start, type:'eval', case:c, candSha, design, label, start, count:5}, k % 5 === 0 ? 'kaggle' : 'github');
  }
}
const dirs = L.directions(3).dirs;
if (dirs.length !== 54) throw Error('Unexpected Case 3 direction count');
const previouslySearched = new Set(JSON.parse(fs.readFileSync('jobs/case3-dirs.json', 'utf8')).map(x => x.dir));
for (const [idx, dir] of dirs.entries()) {
  if (previouslySearched.has(dir)) continue;
  for (const fleet of [21,22,23]) for (const only of ['shortFar','longFar']) {
    const id = batch + ':c3:' + idx + ':' + fleet + ':' + only;
    const seed = 251100 + idx * 7 + fleet;
    add({id, type:'search', case:3, dir, fleet, only, budget:240, seed, label:id}, idx % 6 === 0 ? 'kaggle' : 'github');
  }
}
for (const [idx, tasks] of gh.entries()) fs.writeFileSync('jobs/' + batch + '-g' + idx + '.jsonl', tasks.map(t => JSON.stringify(t)).join('\n') + '\n');
fs.writeFileSync('jobs/' + batch + '.jsonl', kaggle.map(t => JSON.stringify(t)).join('\n') + '\n');
console.log({github:gh.map(a => a.length), kaggle:kaggle.length, newCase3Directions:dirs.length - previouslySearched.size});
