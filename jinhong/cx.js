// C++ 엔진(cpp/agvx.exe)을 띄워 두고 작업 묶음을 보내는 연결 코드.
// 비용 단위는 「시드 5개 묶음」이다. 웹처럼 묶음 안의 대기시간을 모아 평균 · 긴급 95분위를 계산한다.
const { spawn } = require('child_process');
const path = require('path'), fs = require('fs');
const DIR = '000011110000000111100000000110101';

function csvToSpec(text) {
  let n = 0; const nodes = [];
  for (const l of text.trim().split(/\r?\n/)) {
    const f = l.trim().split(',');
    if (f[0] === 'fleet') n = +f[1];
    if (f[0] === 'key') nodes.push(f.slice(1).join(','));
  }
  return { n, nodes: nodes.join(';') };
}
function specToCsv(s) { return [`dir,${DIR}`, `fleet,${s.n}`, ...s.nodes.split(';').map(x => 'key,' + x)].join('\n') + '\n'; }

// Node 엔진(jsim.js) 워커 풀. 실행 파일이 필요 없어 기본으로 쓴다. 인터페이스는 C++ 판과 같다.
class JsEngine {
  constructor(threads) {
    const { Worker } = require('worker_threads');
    this.ws = Array.from({ length: threads || 10 }, () => new Worker(path.join(__dirname, 'jsim.js')));
    this.idle = [...this.ws]; this.q = []; this.cb = new Map(); this.tid = 0;
    this.ws.forEach(w => w.on('message', m => { this.cb.get(m.tid)(m.v); this.cb.delete(m.tid); this.idle.push(w); this._pump(); }));
  }
  _pump() { while (this.idle.length && this.q.length) this.idle.pop().postMessage(this.q.shift()); }
  _task(t) { return new Promise(res => { t.tid = this.tid++; this.cb.set(t.tid, res); this.q.push(t); this._pump(); }); }
  async run(jobs) {
    return Promise.all(jobs.map(j => {
      const block = j.block || 5, nb = Math.ceil(j.count / block);
      return Promise.all(Array.from({ length: nb }, (_, b) => this._task({ n: j.n, nodes: j.nodes, seed0: j.seed0 + b * block, cnt: Math.min(block, j.count - b * block), qcut: j.qcut || 0 })));
    }));
  }
  close() { this.ws.forEach(w => w.terminate()); }
}

class CppEngine {
  constructor(threads) {
    const exe = process.env.AGVX_EXE || path.join(__dirname, 'cpp', 'agvx.exe');
    this.p = spawn(exe, [path.join(__dirname, 'cpp', 'case.txt'), String(threads || 14)]);
    this.buf = ''; this.lines = []; this.waiters = [];
    this.p.stdout.on('data', d => {
      this.buf += d; let i;
      while ((i = this.buf.indexOf('\n')) >= 0) { this.lines.push(this.buf.slice(0, i).trim()); this.buf = this.buf.slice(i + 1); }
      this._pump();
    });
    this.p.stderr.on('data', d => process.stderr.write(d));
  }
  _pump() { while (this.waiters.length && this.waiters[0].test(this.lines)) this.waiters.shift().done(); }
  _wait(test) { return new Promise(res => { this.waiters.push({ test, done: res }); this._pump(); }); }
  // jobs: [{n, nodes, seed0, count, block=5, qcut=0}] → 각 작업의 묶음 비용 배열(반려는 Infinity)
  async run(jobs) {
    if (!jobs.length) return [];
    let s = '';
    jobs.forEach((j, i) => { s += `J ${i} ${j.n} ${j.seed0} ${j.count} ${j.block || 5} ${j.qcut || 0} ${j.nodes}\n`; });
    this.p.stdin.write(s + 'GO\n');
    await this._wait(ls => ls.includes('DONE'));
    const out = new Array(jobs.length).fill(null), k = this.lines.indexOf('DONE');
    for (const l of this.lines.splice(0, k + 1)) {
      if (!l.startsWith('R ')) continue;
      const f = l.split(' ');
      out[+f[1]] = f[2] === 'err' ? null : f.slice(2).map(x => x === 'inf' ? Infinity : +x);
    }
    return out;
  }
  async diag(spec, seed0, count) {
    this.p.stdin.write(`D ${spec.n} ${seed0} ${count} ${spec.nodes}\n`);
    await this._wait(ls => ls.some(l => l.startsWith('{')));
    const i = this.lines.findIndex(l => l.startsWith('{'));
    return JSON.parse(this.lines.splice(i, 1)[0]);
  }
  close() { this.p.stdin.end(); }
}
// AGVX_EXE를 주면 C++ 엔진, 아니면 Node 엔진. (11/23: 스마트 앱 컨트롤이 새 exe를 막아 Node가 기본)
const Engine = process.env.AGVX_EXE ? CppEngine : JsEngine;
const mean = a => a.reduce((s, x) => s + x, 0) / a.length;
// 같은 묶음끼리 짝지은 차이의 평균, 표준오차, t
function paired(a, b) {
  const d = a.map((x, i) => x - b[i]), m = mean(d);
  const se = Math.sqrt(d.reduce((s, x) => s + (x - m) ** 2, 0) / (d.length - 1) / d.length);
  return { d: m, se, t: m / se, wins: d.filter(x => x < 0).length, n: d.length };
}
module.exports = { Engine, csvToSpec, specToCsv, mean, paired, DIR, readSpec: f => csvToSpec(fs.readFileSync(f, 'utf8')) };
