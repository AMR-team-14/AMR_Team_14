// Vendored from AMR-team-14/AMR_Team_14 jinhong/jsim.js, Git blob c2f9a2aec822b587cb08346e0629f63e54ac69a8
// jsim — 빠른 판 엔진(cpp/agvx.cpp 2판)을 그대로 Node로 옮긴 것. 실행 파일이 필요 없다.
// (11/23: Windows 스마트 앱 컨트롤이 새로 빌드한 exe를 차단해서 만들었다.)
// 사건 순서 · 실수 연산 순서는 웹 원본과 같다. 검증: node cx_verify.js
const E = require('./web/engine_raw.js');
const P = E.DATA_AGV;
const NET = E.agvNet_(P, P.fixedDir.split('').map(Number));
const nd = P.pd.length, PD = P.pd, F = P.flows, nF = F.length;
const deg = new Array(P.nodes.length).fill(0); P.edges.forEach(e => { deg[e[0]]++; deg[e[1]]++; });
const JIDX = new Int32Array(P.nodes.length).fill(-1); let nJ = 0;
for (let n = 0; n < deg.length; n++) if (deg[n] >= 3) JIDX[n] = nJ++;
const DIST = []; const ROUTE = [];
for (let a = 0; a < nd; a++) {
  DIST.push(new Float64Array(nd)); ROUTE.push([]);
  for (let b = 0; b < nd; b++) {
    DIST[a][b] = NET.D[PD[a]][PD[b]];
    const jn = [], la = []; let c = PD[a];
    while (c !== PD[b]) { c = NET.NX[c][PD[b]]; if (c !== PD[b] && JIDX[c] >= 0) { jn.push(JIDX[c]); la.push(NET.D[PD[a]][c]); } }
    ROUTE[a].push({ nl: jn.length, jn: Int32Array.from(jn), la: Float64Array.from(la), total: NET.D[PD[a]][PD[b]] });
  }
}
let RATE = 0; for (let f = 0; f < nF; f++) RATE += F[f][2];
const FAC = 60 / P.at, MEAN = 60 / (P.alpha * RATE);
const FO = Int32Array.from(F.map(f => f[0])), FD = Int32Array.from(F.map(f => f[1])), FR = Float64Array.from(F.map(f => f[2]));
let LAM = 0; F.forEach(f => { LAM += f[2]; }); LAM = P.alpha * LAM;
const IDLE = 0, DRV = 1, WQ = 2, CRS = 3, LD = 4, UL = 5;
const FI = { urg: 0, w: 1, de: 2, dl: 3, qo: 4 };

// "id,f,dir,th,lo,hi;..." → 나무 배열. 앞에 "T<49칸>.<49칸>#"이 붙으면 형식 밖 가상 정책 표(실험 전용).
function parseSpec(n, nodes) {
  const ds = { n, f: new Int32Array(64), mx: new Int32Array(64), th: new Float64Array(64), lo: new Int32Array(64), hi: new Int32Array(64), tab: null };
  let s = nodes;
  if (s[0] === 'T') { const h = s.indexOf('#'), v = s.slice(1, h).split('.').map(Number); s = s.slice(h + 1); ds.tab = [Float64Array.from(v.slice(0, nd * nd)), Float64Array.from(v.slice(nd * nd))]; }
  for (const it of s.split(';')) { if (!it) continue; const x = it.split(','), id = +x[0]; ds.f[id] = FI[x[1]]; ds.mx[id] = x[2] === 'max' ? 1 : 0; ds.th[id] = +x[3]; ds.lo[id] = +x[4]; ds.hi[id] = +x[5]; }
  return ds;
}

const key = new Float64Array(16), bk = new Float64Array(16);
// 반환: {unstable, maxQ, endQ}. 대기시간은 ws · wu 배열에 덧붙인다.
function simulate(ds, seed, qcut, ws, wu, onDispatch) {
  const n = ds.n, f_ = ds.f, mx = ds.mx, th = ds.th, lo = ds.lo, hi = ds.hi, tab = ds.tab;
  let rs = seed | 0;
  const R = () => { rs = (rs + 0x6D2B79F5) | 0; let t = Math.imul(rs ^ (rs >>> 15), 1 | rs); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
  const EXP = () => { let k = 0; for (;;) { const u = R(); let prev = u, m = 1; for (;;) { const v = R(); if (v < prev) { prev = v; m++; } else break; } if (m % 2 === 1) return k + u; k++; } };
  const vnode = new Int32Array(n), vst = new Int32Array(n), vph = new Int32Array(n), vto = new Int32Array(n), vk = new Int32Array(n);
  const vspd = new Float64Array(n), vpos = new Float64Array(n), vd1 = new Float64Array(n), tact = new Float64Array(n).fill(Infinity);
  const vrt = new Array(n), vro = new Int32Array(n), vrd = new Int32Array(n), vrtm = new Float64Array(n);
  const jbusy = new Int32Array(nJ).fill(-1), jq = new Int32Array(nJ * 64), jh = new Int32Array(nJ), jt = new Int32Array(nJ);
  let qo_ = new Int32Array(512), qd_ = new Int32Array(512), qt_ = new Float64Array(512), qi_ = new Int32Array(512), ql = 0;
  for (let i = 0; i < n; i++) vnode[i] = i % nd;
  let nIdle = n, nWq = 0, t = 0, serial = 0, maxQ = 0, next = EXP() * MEAN;
  const idle = new Int32Array(n), qcnt = new Int32Array(nd);
  const T = P.T, warm = P.warm, UD = P.urgentDest, tlf = P.tl * FAC, tuf = P.tu * FAC, pf = P.p * FAC, qcap = P.qcap, vc = P.vc, ve = P.ve;

  function startLeg(i) { const rt = vrt[i], d1 = vk[i] < rt.nl ? rt.la[vk[i]] : rt.total; vst[i] = DRV; vd1[i] = d1; tact[i] = t + (d1 - vpos[i]) / vspd[i] * FAC; }
  function travel(i, ph, to, spd) { vrt[i] = ROUTE[vnode[i]][to]; vph[i] = ph; vto[i] = to; vspd[i] = spd; vk[i] = 0; vpos[i] = 0; startLeg(i); }
  function handle(i) {
    const st = vst[i];
    if (st === DRV) {
      vpos[i] = vd1[i];
      const rt = vrt[i];
      if (vk[i] < rt.nl) { vst[i] = WQ; tact[i] = Infinity; const j = rt.jn[vk[i]]; jq[j * 64 + (jt[j]++ & 63)] = i; nWq++; return; }
      vnode[i] = vto[i];
      if (vph[i] === 0) {
        const w = t - vrtm[i];
        if (vrtm[i] >= warm) { ws.push(w); if (vrd[i] === UD) wu.push(w); }
        vst[i] = LD; tact[i] = t + tlf;
      } else { vst[i] = UL; tact[i] = t + tuf; }
    } else if (st === CRS) { jbusy[vrt[i].jn[vk[i]]] = -1; vk[i]++; startLeg(i); }
    else if (st === LD) travel(i, 1, vrd[i], vc);
    else if (st === UL) { vst[i] = IDLE; tact[i] = Infinity; nIdle++; }
  }
  function dispatch() {
    while (nIdle && ql) {
      let ni = 0;
      for (let m = 0; m < n; m++) if (vst[m] === IDLE) idle[ni++] = m;
      qcnt.fill(0);
      for (let m = 0; m < ql; m++) qcnt[qo_[m]]++;
      let bv = -1, bb = -1, bl = 0;
      for (let a = 0; a < ni; a++) {
        const vi = idle[a], vn = vnode[vi];
        for (let b = 0; b < ql; b++) {
          const ro = qo_[b], de = DIST[vn][ro];
          let kl = 0, id = 1;
          if (tab) key[kl++] = ((t - qt_[b]) <= 1e-9 ? tab[0] : tab[1])[vn * nd + ro];
          for (let g = 0; g < 8 && id; g++) {
            const ff = f_[id]; let side;
            if (ff === 0) side = qd_[b] === UD ? 1 : 0;
            else if (ff === 1) side = (t - qt_[b]) <= th[id] ? 0 : 1;
            else side = (ff === 2 ? de : ff === 3 ? DIST[ro][qd_[b]] : qcnt[ro]) <= th[id] ? 0 : 1;
            key[kl++] = mx[id] ? 1 - side : side;
            id = side ? hi[id] : lo[id];
          }
          key[kl++] = de; key[kl++] = qt_[b]; key[kl++] = qi_[b]; key[kl++] = vi;
          let less = bv < 0;
          if (!less) for (let i = 0; i < kl; i++) { if (i >= bl) continue; if (key[i] < bk[i]) { less = true; break; } if (key[i] > bk[i]) break; }
          if (less) { for (let c = 0; c < kl; c++) bk[c] = key[c]; bl = kl; bv = vi; bb = b; }
        }
      }
      if (onDispatch) onDispatch(qi_[bb], bv, t);
      vro[bv] = qo_[bb]; vrd[bv] = qd_[bb]; vrtm[bv] = qt_[bb];
      for (let m = bb; m < ql - 1; m++) { qo_[m] = qo_[m + 1]; qd_[m] = qd_[m + 1]; qt_[m] = qt_[m + 1]; qi_[m] = qi_[m + 1]; }
      ql--; nIdle--;
      travel(bv, 0, vro[bv], ve);
    }
  }
  for (;;) {
    let tn = next;
    for (let m = 0; m < n; m++) if (tact[m] < tn) tn = tact[m];
    if (tn > T) break;
    t = tn;
    while (next <= t) {
      let u = R() * RATE, k = 0;
      while (k < nF - 1 && u >= FR[k]) { u -= FR[k]; k++; }
      if (ql >= qo_.length) return { unstable: true, maxQ: ql, endQ: ql };
      qo_[ql] = FO[k]; qd_[ql] = FD[k]; qt_[ql] = next; qi_[ql] = serial++; ql++;
      next = next + EXP() * MEAN;
    }
    for (;;) {
      for (let i = 0; i < n; i++) while (tact[i] <= t) handle(i);
      if (nWq) for (let g = 0; g < nJ; g++) {
        if (jbusy[g] >= 0 || jh[g] === jt[g]) continue;
        const i = jq[g * 64 + (jh[g]++ & 63)];
        vst[i] = CRS; tact[i] = t + pf; jbusy[g] = i; nWq--;
      }
      dispatch();
      let any = false;
      for (let i = 0; i < n; i++) if (tact[i] <= t) { any = true; break; }
      if (!any) break;
    }
    if (ql > maxQ) maxQ = ql;
    if (ql > qcap || (qcut > 0 && ql > qcut)) return { unstable: true, maxQ, endQ: ql };
  }
  return { unstable: ql > P.endQmax, maxQ, endQ: ql };
}

// 웹 agvEvaluate_와 같은 집계: 묶음 안 시드들의 대기시간을 모아 평균 · 긴급 95분위를 한 번 계산
function blockCost(ds, seed0, cnt, qcut) {
  const ws = [], wu = [];
  for (let s = 0; s < cnt; s++) { const o = simulate(ds, seed0 + s, qcut, ws, wu); if (o.unstable) return Infinity; }
  let sum = 0; for (let k = 0; k < ws.length; k++) sum += ws[k];
  const u = Float64Array.from(wu).sort();
  const mean = ws.length ? sum / ws.length : 0, u95 = u.length ? u[Math.floor(0.95 * (u.length - 1))] : 0;
  return P.cAGV * ds.n + P.cW * LAM * mean + P.cU * u95;
}
module.exports = { parseSpec, simulate, blockCost };

// 워커로 뜨면 (작업, 묶음) 단위로 받아 비용을 돌려준다.
const { parentPort, isMainThread } = require('worker_threads');
if (!isMainThread && parentPort) {
  const cache = new Map();
  parentPort.on('message', m => {
    const k = m.n + '|' + m.nodes; let ds = cache.get(k);
    if (!ds) { ds = parseSpec(m.n, m.nodes); if (cache.size > 2000) cache.clear(); cache.set(k, ds); }
    parentPort.postMessage({ tid: m.tid, v: blockCost(ds, m.seed0, m.cnt, m.qcut) });
  });
}
