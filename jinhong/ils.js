// ils — 방향 반복 국소 탐색(대수 고정). 구간 62개처럼 전수가 안 되는 지도에서 「다른 골짜기」를 찾는 용도(Case 4, 11/25).
//  방향 하나의 점수 = 형식 안 규칙 묶음의 최저:  urg(max)[dl(min, 상위 k묶음 경계)[-, de(max,R)], -]  (k = 1..KH, R ∈ RS) + 가까운 차 먼저.
//  한 구간 뒤집기 이웃 전부를 한 번에 평가해 가장 낮은 곳으로 내려가고(바닥까지), 바닥에서 구간 3~7개를 무작위로 뒤집어(차기) 다시 내려간다.
//  node ils.js <케이스> <시작 csv> <대수> <분> [씨앗=1] [KH=4] [반경들=75,90,105]
//   → caseN/ils_n<대수>_s<씨앗>.json (바닥 목록, 점수 순) · ils_n<대수>_s<씨앗>_<i>.csv (상위 6개) · ils.log
const fs = require('fs'), path = require('path');
const { Engine, loadCase, strong, readSpec, specToCsv } = require('./gx.js');
const [CN, startF, n0, MIN, SEED = '1', KH = '4', RS0 = '75,90,105'] = process.argv.slice(2), P = loadCase(CN), OUT = path.join(__dirname, 'case' + CN);
const n = +n0, RS = RS0.split(',').map(Number), N = P.nodes.length, E = P.edges.length, TAG = `ils${process.env.ILS_CYC === '1' ? 'c' : ''}_n${n}_s${SEED}`;
const log = s => { console.log(s); fs.appendFileSync(path.join(OUT, 'ils.log'), s + '\n'); };
let seed = +SEED * 2654435761 % 4294967296; const rnd = () => { seed = (seed * 1664525 + 1013904223) % 4294967296; return seed / 4294967296; };
// 구간(차수 2 교차로로 이어진 통로 묶음)
const CH = (() => { const inc = Array.from({ length: N }, () => []), par = P.edges.map((_, k) => k); P.edges.forEach((e, k) => { inc[e[0]].push(k); inc[e[1]].push(k); }); const find = x => par[x] === x ? x : (par[x] = find(par[x])); inc.forEach(l => { if (l.length === 2) par[find(l[0])] = find(l[1]); }); const g = new Map(); P.edges.forEach((_, k) => { const r = find(k); if (!g.has(r)) g.set(r, []); g.get(r).push(k); }); return [...g.values()]; })();
const flip = (dir, cs) => { const b = dir.split(''); for (const c of cs) for (const k of CH[c]) b[k] = b[k] === '1' ? '0' : '1'; return b.join(''); };
// 순환 뒤집기 이웃(ILS_CYC=1): 구간마다 그 구간을 지나는 가장 짧은 방향 순환을 찾아 통째로 뒤집는다. 방향 순환을 뒤집어도 강연결은 그대로라, 한 구간 뒤집기로는 못 가는 방향으로 간다.
const CYC = process.env.ILS_CYC === '1';
function cycleNb(dir) {
  const out = Array.from({ length: N }, () => []); for (let k = 0; k < E; k++) { const e = P.edges[k], a = dir[k] === '1' ? e[1] : e[0], b = dir[k] === '1' ? e[0] : e[1]; out[a].push([b, k]); }
  const res = new Map();
  for (let k0 = 0; k0 < E; k0++) { const e = P.edges[k0], a = dir[k0] === '1' ? e[1] : e[0], b = dir[k0] === '1' ? e[0] : e[1];   // a→b 를 지나는 가장 짧은 순환 = b에서 a로 가는 가장 짧은 길
    const prev = new Int32Array(N).fill(-2), pe = new Int32Array(N).fill(-1), q = [b]; prev[b] = -1;
    for (let h = 0; h < q.length && prev[a] === -2; h++) for (const [y, k] of out[q[h]]) if (prev[y] === -2) { prev[y] = q[h]; pe[y] = k; q.push(y); }
    if (prev[a] === -2) continue; const ks = [k0]; for (let x = a; x !== b; x = prev[x]) ks.push(pe[x]);
    const key = ks.slice().sort((x, y) => x - y).join(','); if (!res.has(key)) { const bb = dir.split(''); for (const k of ks) bb[k] = bb[k] === '1' ? '0' : '1'; res.set(key, bb.join('')); } }
  return [...res.values()];
}
const D = new Float64Array(N * N);
function family(dir) {   // 이 방향의 규칙 묶음(나무 문자열 = 엔진 노드 표기)
  D.fill(Infinity); for (let i = 0; i < N; i++) D[i * N + i] = 0;
  for (let k = 0; k < E; k++) { const e = P.edges[k], a = dir[k] === '1' ? e[1] : e[0], b = dir[k] === '1' ? e[0] : e[1]; D[a * N + b] = e[2]; }
  for (let k = 0; k < N; k++) for (let i = 0; i < N; i++) { const dik = D[i * N + k]; if (dik === Infinity) continue; const ik = i * N, kk = k * N; for (let j = 0; j < N; j++) { const d = dik + D[kk + j]; if (d < D[ik + j]) D[ik + j] = d; } }
  const dn = [...new Set(P.flows.filter(x => x[1] !== P.urgentDest).map(x => +D[P.pd[x[0]] * N + P.pd[x[1]]].toFixed(4)))].sort((a, b) => a - b), out = ['1,urg,max,0.5,0,0'];
  for (let k = 1; k <= +KH; k++) { const i = dn.length - k - 1; if (i < 0) break; for (const R of RS) out.push(`1,urg,max,0.5,2,0;2,dl,min,${dn[i]},0,3;3,de,max,${R},0,0`); }
  return out;
}
(async () => {
  const eng = new Engine(CN, +(process.env.AGVX_THREADS || 14)), t0 = Date.now(), el = () => (Date.now() - t0) / 60000, cache = new Map(); let evals = 0;
  async function score(dirs) {   // 캐시에 없는 방향만 평가
    const need = [...new Set(dirs)].filter(d => !cache.has(d)), jobs = [], idx = [];
    for (const d of need) { const fam = family(d); fam.forEach(nodes => { jobs.push({ dir: d, n, nodes }); idx.push(d); }); }
    const r = jobs.length ? await eng.pub(jobs) : []; evals += jobs.length;
    for (let i = 0; i < jobs.length; i++) { const c = cache.get(idx[i]); if (!c || r[i] < c.pub) cache.set(idx[i], { pub: r[i], nodes: jobs[i].nodes }); }
    return dirs.map(d => cache.get(d));
  }
  const POOL = process.env.ILS_POOL ? JSON.parse(fs.readFileSync(path.join(OUT, process.env.ILS_POOL), 'utf8')).cands.map(c => c.dir) : []; let pi = 0;
  const mins = new Map();   // 바닥: dir → {pub, nodes}
  const save = () => { const top = [...mins.entries()].map(([dir, v]) => ({ dir, ...v })).sort((a, b) => a.pub - b.pub); fs.writeFileSync(path.join(OUT, TAG + '.json'), JSON.stringify({ n, evals, minutes: +el().toFixed(1), cands: top.slice(0, 200) })); top.slice(0, 6).forEach((c, i) => fs.writeFileSync(path.join(OUT, `${TAG}_${i + 1}.csv`), specToCsv({ dir: c.dir, n, nodes: c.nodes }))); return top; };
  let cur = readSpec(startF).dir, cs = (await score([cur]))[0], best = { dir: cur, pub: cs.pub }, it = 0;
  log(`[방향 반복 국소 탐색 ${TAG}] Case ${CN}, ${n}대, 구간 ${CH.length}개, 규칙 묶음 ${family(cur).length}개, 시작 ${cs.pub.toFixed(2)}, ${MIN}분`);
  while (el() < +MIN) {
    // 내려가기(한 구간 뒤집기 이웃 전부 → 가장 낮은 곳)
    for (; el() < +MIN;) {
      const nb = []; for (let c = 0; c < CH.length; c++) { const d = flip(cur, [c]); if (strong(P, d)) nb.push(d); }
      if (CYC) for (const d of cycleNb(cur)) if (strong(P, d)) nb.push(d);
      const r = await score(nb); let bi = -1, bv = cache.get(cur).pub; r.forEach((x, i) => { if (x.pub < bv - 1e-9) { bv = x.pub; bi = i; } });
      if (bi < 0) break; cur = nb[bi];
    }
    const c = cache.get(cur); if (!mins.has(cur)) mins.set(cur, { pub: c.pub, nodes: c.nodes }); it++;
    if (c.pub < best.pub - 1e-9) { best = { dir: cur, pub: c.pub }; log(`  [${el().toFixed(1)}분, 바닥 ${mins.size}개, 평가 ${evals}] 새 최저 ${c.pub.toFixed(2)} | ${cur} | ${c.nodes}`); }
    if (it % 10 === 0) { const top = save(); log(`  [${el().toFixed(1)}분] 바닥 ${mins.size}개, 평가 ${evals}, 상위: ${top.slice(0, 6).map(x => x.pub.toFixed(2)).join(' ')}`); }
    if (POOL.length) { let nd0 = null; while (pi < POOL.length && !nd0) { const d = POOL[pi++]; if (strong(P, d) && !mins.has(d)) nd0 = d; } if (!nd0) break; cur = nd0; const c0 = (await score([cur]))[0]; log(`  [${el().toFixed(1)}분] 출발점 ${pi}/${POOL.length}: ${c0.pub.toFixed(2)}`); continue; }   // ILS_POOL: 차는 대신 풀의 다음 방향에서 다시 내려간다
    // 차기: 최저 바닥(절반) 또는 지금 바닥에서 구간 3~7개를 무작위로 뒤집는다
    const base = rnd() < 0.5 ? best.dir : cur; let nd = null;
    for (let tr = 0; tr < 200 && !nd; tr++) { let d = base; if (CYC) { const k0 = +(process.env.ILS_KMIN || 2), k1 = +(process.env.ILS_KMAX || 4), m = k0 + Math.floor(rnd() * (k1 - k0 + 1)); for (let j = 0; j < m; j++) { const cy = cycleNb(d); d = cy[Math.floor(rnd() * cy.length)]; } }   // 순환 2~4개를 잇달아 뒤집는다
      else { const k = 3 + Math.floor(rnd() * 5), cs2 = new Set(); while (cs2.size < k) cs2.add(Math.floor(rnd() * CH.length)); d = flip(base, [...cs2]); }
      if (strong(P, d) && !mins.has(d)) nd = d; }
    if (!nd) break; cur = nd; await score([cur]);
  }
  const top = save(); log(`[끝 ${TAG}] ${el().toFixed(1)}분, 바닥 ${mins.size}개, 평가 ${evals}, 상위: ${top.slice(0, 8).map(x => x.pub.toFixed(2)).join(' ')}`); eng.close();
})();
