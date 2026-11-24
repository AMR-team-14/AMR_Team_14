// gx — 케이스 일반판 연결 코드(cx.js의 Case N 판). 케이스 JSON을 읽어 C++ 엔진(cpp/agvg.exe)에 작업을 보낸다.
// 케이스 JSON 위치: 환경변수 AGV_CASES(기본 C:/dev/ie325-amr/cases) 아래 final-agv-cN.json. 리포에는 올리지 않는다.
// 비용 단위는 「시드 묶음」이다. 웹처럼 묶음 안의 대기시간을 모아 평균 · 긴급 95분위를 계산한다.
const { spawn } = require('child_process');
const path = require('path'), fs = require('fs'), os = require('os');
const CASES = process.env.AGV_CASES || 'C:/dev/ie325-amr/cases';

function loadCase(c) {
  const f = /\.json$/.test(String(c)) ? String(c) : path.join(CASES, `final-agv-c${c}.json`);
  return JSON.parse(fs.readFileSync(f, 'utf8'));
}
// 엔진이 읽는 글자 파일(임시 폴더)
function caseTxt(P, tag) {
  const L = [`${P.nodes.length} ${P.edges.length} ${P.pd.length} ${P.flows.length}`, P.pd.join(' ')];
  P.edges.forEach(e => L.push(`${e[0]} ${e[1]} ${e[2]}`));
  P.flows.forEach(f => L.push(f.join(' ')));
  L.push([P.urgentDest, P.alpha, P.vc, P.ve, P.tl, P.tu, P.at, P.p, P.T, P.warm, P.qcap, P.endQmax, P.cAGV, P.cW, P.cU].join(' '));
  const f = path.join(os.tmpdir(), `agvg_case_${tag}_${process.pid}.txt`);
  const body = L.join('\n') + '\n';   // 같은 프로세스에서 엔진을 여러 개 띄울 때 다시 쓰면 먼저 뜬 엔진이 잘린 파일을 읽는다(「case 읽기 실패」) → 내용이 같으면 쓰지 않는다
  if (!(fs.existsSync(f) && fs.readFileSync(f, 'utf8') === body)) fs.writeFileSync(f, body);
  return f;
}
// 강연결 검사(웹 agvNet_의 ok와 같은 뜻)
function strong(P, bits) {
  const N = P.nodes.length, out = Array.from({ length: N }, () => []), inn = Array.from({ length: N }, () => []);
  P.edges.forEach((e, k) => { const a = bits[k] === '1' ? e[1] : e[0], b = bits[k] === '1' ? e[0] : e[1]; out[a].push(b); inn[b].push(a); });
  const reach = adj => { const s = new Uint8Array(N); s[0] = 1; const st = [0]; let c = 1; while (st.length) { const x = st.pop(); for (const y of adj[x]) if (!s[y]) { s[y] = 1; c++; st.push(y); } } return c === N; };
  return reach(out) && reach(inn);
}
function csvToSpec(text) {
  let n = 0, dir = ''; const nodes = [];
  for (const l of text.trim().split(/\r?\n/)) {
    const f = l.trim().split(',');
    if (f[0] === 'dir') dir = f[1];
    if (f[0] === 'fleet') n = +f[1];
    if (f[0] === 'key') nodes.push(f.slice(1).join(','));
  }
  return { dir, n, nodes: nodes.join(';') };
}
const specToCsv = s => [`dir,${s.dir}`, `fleet,${s.n}`, ...s.nodes.split(';').map(x => 'key,' + x)].join('\n') + '\n';

class Engine {
  constructor(c, threads) {
    this.P = typeof c === 'object' ? c : loadCase(c);
    const exe = process.env.AGVG_EXE || path.join(__dirname, 'cpp', 'agvg.exe');
    this.txt = caseTxt(this.P, typeof c === 'object' ? 'obj' : String(c).replace(/[^A-Za-z0-9]/g, '_'));
    this.p = spawn(exe, [this.txt, String(threads || process.env.AGVX_THREADS || 14)]);
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
  // jobs: [{dir, n, nodes, seed0, count, block=5, qcut=0}] → 각 작업의 묶음 비용 배열(반려 · 강연결 아님은 Infinity)
  async run(jobs) {
    if (!jobs.length) return [];
    let s = '';
    jobs.forEach((j, i) => { s += `J ${i} ${j.n} ${j.seed0} ${j.count} ${j.block || 5} ${j.qcut || 0} ${j.dir} ${j.nodes}\n`; });
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
  // 공개 시드 점수(웹 「평가 실행」과 같은 값, 소수 둘째 자리 반올림 전). 반려는 Infinity
  async pub(specs, qcut = 0) {
    const S = this.P.seeds; // 공개 시드는 연속 정수라고 가정하고, 아니면 멈춘다
    for (let i = 1; i < S.length; i++) if (S[i] !== S[0] + i) throw new Error('공개 시드가 연속이 아님: ' + S);
    const r = await this.run(specs.map(s => ({ ...s, seed0: S[0], count: S.length, block: S.length, qcut })));
    return r.map(x => x ? x[0] : Infinity);
  }
  async detail(spec, seed0, count) {
    this.p.stdin.write(`E ${spec.n} ${seed0} ${count} ${spec.dir} ${spec.nodes}\n`);
    await this._wait(ls => ls.some(l => l.startsWith('{')));
    const i = this.lines.findIndex(l => l.startsWith('{'));
    return JSON.parse(this.lines.splice(i, 1)[0]);
  }
  close() { this.p.stdin.end(); try { fs.unlinkSync(this.txt); } catch (e) { } }
}
const mean = a => a.reduce((s, x) => s + x, 0) / a.length;
const round2 = x => Math.round(x * 100) / 100;
function paired(a, b) {
  const d = a.map((x, i) => x - b[i]), m = mean(d);
  const se = Math.sqrt(d.reduce((s, x) => s + (x - m) ** 2, 0) / (d.length - 1) / d.length);
  return { d: m, se, t: m / se, wins: d.filter(x => x < 0).length, n: d.length };
}
module.exports = { Engine, loadCase, strong, csvToSpec, specToCsv, mean, paired, round2, CASES, readSpec: f => csvToSpec(fs.readFileSync(f, 'utf8')) };
