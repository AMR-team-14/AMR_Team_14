// Task runner shared by local, GitHub Actions and Kaggle.
//   node run.cjs <tasks.jsonl> --env local|github|kaggle --workers N --out <dir>
// Runs only tasks assigned to --env, skips task ids already present in <out>/*.jsonl (resume), and forks N
// single-thread child processes. Each finished task is appended immediately to <out>/<batch>-<env>.jsonl.
'use strict';
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {fork, execSync} = require('node:child_process');

if (process.argv[2] === '--child') {
  const L = require('./lib.cjs');
  const {runSearch} = require('./search.cjs');
  process.on('message', task => {
    const t0 = Date.now();
    let out;
    try {
      if (task.type === 'eval') {
        const cand = L.candidate(task.case, L.parseDesign(task.design));
        if (cand.sha !== task.candSha) throw Error('Candidate hash mismatch');
        const e = L.evaluate(task.case, cand.design, task.start, task.count);
        if (task.expect !== undefined && (e.score === null || e.score.toFixed(2) !== task.expect)) throw Error('Baseline mismatch ' + e.score);
        out = {status: 'done', ...e};
      } else if (task.type === 'search') {
        out = {status: 'done', ...runSearch(task)};
      } else if (task.type === 'ils') {
        out = {status: 'done', ...require('./ils.cjs').runIls(task)};
      } else throw Error('Unknown task type');
    } catch (err) {
      out = {status: 'failed', error: String(err && err.message || err)};
    }
    process.send({id: task.id, ms: Date.now() - t0, out});
  });
  return;
}

const args = process.argv.slice(2);
const opt = name => { const i = args.indexOf('--' + name); return i >= 0 ? args[i + 1] : undefined; };
const tasksFile = args[0];
const env = opt('env');
const workers = Math.max(1, Number(opt('workers') || 1));
const outDir = opt('out') || 'out';
if (!tasksFile || !env) throw Error('usage: node run.cjs <tasks.jsonl> --env NAME --workers N --out DIR');

const L = require('./lib.cjs');
const sh = cmd => { try { return execSync(cmd, {encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore']}).trim(); } catch { return ''; } };
const info = {
  env, host: os.hostname(), platform: process.platform, node: process.version, cpus: os.cpus().length,
  cpuModel: os.cpus()[0] && os.cpus()[0].model, nproc: sh('nproc'), cpuMax: sh('cat /sys/fs/cgroup/cpu.max'),
  workers, engine: L.ENGINE, started: new Date().toISOString(),
};
const lines = fs.readFileSync(tasksFile, 'utf8').split(/\r?\n/).filter(Boolean).map(JSON.parse);
const batch = lines[0] && lines[0].batch;
fs.mkdirSync(outDir, {recursive: true});
const outFile = path.join(outDir, `${batch}-${env}.jsonl`);
const done = new Set();
for (const f of fs.readdirSync(outDir)) {
  if (!f.endsWith('.jsonl')) continue;
  for (const row of fs.readFileSync(path.join(outDir, f), 'utf8').split(/\r?\n/).filter(Boolean)) {
    const r = JSON.parse(row);
    if (r.id && r.out && r.out.status === 'done') done.add(r.id);
  }
}
const todo = lines.filter(t => t.env === env && !done.has(t.id));
info.assigned = lines.filter(t => t.env === env).length;
info.skipped = info.assigned - todo.length;
fs.writeFileSync(path.join(outDir, `${batch}-${env}.env.json`), JSON.stringify(info, null, 1));
console.log(JSON.stringify(info));

let next = 0, finished = 0;
const t0 = Date.now();
const n = Math.min(workers, todo.length);
if (!n) { console.log('nothing to do'); return; }
for (let w = 0; w < n; w++) {
  const child = fork(__filename, ['--child']);
  const feed = () => { if (next < todo.length) child.send(todo[next++]); else child.disconnect(); };
  child.on('message', m => {
    fs.appendFileSync(outFile, JSON.stringify({id: m.id, batch, env, worker: w, ms: m.ms, out: m.out}) + '\n');
    finished++;
    if (finished % 10 === 0 || finished === todo.length) console.log(`${finished}/${todo.length} ${((Date.now() - t0) / 1000).toFixed(0)}s`);
    feed();
  });
  feed();
}
process.on('exit', () => {
  info.finished = new Date().toISOString();
  info.wallMs = Date.now() - t0;
  fs.writeFileSync(path.join(outDir, `${batch}-${env}.env.json`), JSON.stringify(info, null, 1));
});
