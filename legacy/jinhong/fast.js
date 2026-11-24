// 웹 채점 엔진의 배차 함수만 배열을 새로 만들지 않게 바꾼 빠른 판. 나머지 코드는 웹 원본 그대로다.
// 키 비교 규칙(길이가 다른 키끼리 비교하는 방식 포함)은 agvKey_ + agvLess_와 똑같이 따른다.
// verify()로 웹 원본과 결과가 같은지 확인한다.
const fs = require('fs');
const E = require('./web/engine_raw.js');
const src = fs.readFileSync(__dirname + '/web/engine_raw.js', 'utf8');

const simSrc = src.slice(src.indexOf('function agvSim_('), src.indexOf('function agvRun_('));
const a = simSrc.indexOf('  function dispatch() {'), b = simSrc.indexOf('  function due(v)');
const FAST_DISPATCH = `  var KB = new Float64Array(16), BK = new Float64Array(16), TR = design.tree;
  function dispatch() {
    for (;;) {
      var idle = [], m;
      for (m = 0; m < V.length; m++) if (V[m].st === "idle") idle.push(V[m]);
      if (!idle.length || !q.length) return;
      var qo = [];
      for (m = 0; m < nd; m++) qo.push(0);
      for (m = 0; m < q.length; m++) qo[q[m].o]++;
      var bv = null, bb = -1, bl = 0;
      for (var b = 0; b < q.length; b++) {
        var r = q[b], urg = r.d === prob.urgentDest ? 1 : 0, dl = D[PD[r.o]][PD[r.d]], qq = qo[r.o], age = t - r.t;
        for (var a2 = 0; a2 < idle.length; a2++) {
          var v = idle[a2], de = D[PD[v.node]][PD[r.o]], kl = 0, id = 1;
          for (var g = 0; g < 8 && id; g++) {
            var nd2 = TR[id], side;
            if (nd2.f === "urg") side = urg;
            else if (nd2.f === "w") side = age <= nd2.th ? 0 : 1;
            else side = (nd2.f === "de" ? de : nd2.f === "dl" ? dl : qq) <= nd2.th ? 0 : 1;
            KB[kl++] = nd2.dir === "max" ? 1 - side : side;
            id = side ? nd2.hi : nd2.lo;
          }
          KB[kl++] = de; KB[kl++] = r.t; KB[kl++] = r.id; KB[kl++] = v.id;
          var less = false;
          if (bv === null) less = true;
          else for (var i = 0; i < kl; i++) {
            if (i >= bl) continue;
            if (KB[i] < BK[i]) { less = true; break; }
            if (KB[i] > BK[i]) break;
          }
          if (less) { for (var c = 0; c < kl; c++) BK[c] = KB[c]; bl = kl; bv = v; bb = b; }
        }
      }
      var vv = bv, rr = q.splice(bb, 1)[0];
      if (log) log("disp", vv, rr, D[PD[vv.node]][PD[rr.o]]);
      vv.req = rr; travel(vv, "P", rr.o, prob.ve);
    }
  }
`;
const fastSrc = simSrc.slice(0, a) + FAST_DISPATCH + simSrc.slice(b);
const agvSimFast = new Function('agvRng_', 'agvExp_', 'agvJunctions_', fastSrc + '\nreturn agvSim_;')(
  ...['agvRng_', 'agvExp_', 'agvJunctions_'].map(n => new Function(src.slice(0, src.indexOf('const DATA_AGV')) + `\nreturn ${n};`)()));

const D = E.DATA_AGV, LAM = D.alpha * D.flows.reduce((s, f) => s + f[2], 0);
const NETS = new Map();
function net(dirs) { const k = dirs.join(''); if (!NETS.has(k)) NETS.set(k, E.agvNet_(D, dirs)); return NETS.get(k); }
function runSeed(design, seed) {
  const s = agvSimFast(D, net(design.dirs), design, seed, null);
  while (s.step()) { }
  return s.out;
}
// 웹 agvEvaluate_와 같은 계산: 모든 시드 대기시간을 모아 평균 · 긴급 95분위
function pooled(n, outs) {
  if (outs.some(o => o.unstable)) return { stable: false, value: Infinity };
  let sum = 0, cnt = 0; const wu = [];
  for (const o of outs) { for (const w of o.ws) { sum += w; cnt++; } for (const w of o.wu) wu.push(w); }
  wu.sort((x, y) => x - y);
  const mean = sum / cnt, u95 = wu.length ? wu[Math.floor(0.95 * (wu.length - 1))] : 0;
  const v = D.cAGV * n + D.cW * LAM * mean + D.cU * u95;
  return { stable: true, mean, u95, value: Math.round(v * 100) / 100, raw: v };
}
function evalDesign(design, seeds) { return pooled(design.n, seeds.map(s => runSeed(design, s))); }
module.exports = { runSeed, pooled, evalDesign };

if (require.main === module) { // 검증: 웹 원본과 같은 값이 나오는지
  const { parse } = require('./eval.js');
  for (const f of process.argv.slice(2)) {
    const d = parse(fs.readFileSync(f, 'utf8')), seeds = [11, 12, 13, 14, 15, 101, 102];
    let t0 = Date.now(); const w = E.agvEvaluate_(D, d, seeds); const t1 = Date.now() - t0;
    t0 = Date.now(); const x = evalDesign(d, seeds); const t2 = Date.now() - t0;
    console.log(f.split(/[\\/]/).pop(), '웹', w.value, '빠른판', x.value, w.value === x.value ? '같음' : '다름!', `${t1}ms → ${t2}ms`);
  }
}
