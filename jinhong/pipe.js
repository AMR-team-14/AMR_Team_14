// pipe — Case N 공통 파이프라인. 케이스 번호만 바꿔 같은 순서로 돌린다(자세한 순서는 PIPELINE.md).
//   node pipe.js <N> base                       기준선: 초기 방향 z0 × 예시 규칙 3개(STTF · 긴급 먼저 · FCFS) × 대수 전부
//   node pipe.js <N> dirrank [상위 몇 개=8] [dirs 파일=dirs.json]   방향 후보(Gurobi) × 나무 묶음 × 대수 → 시뮬레이션으로 다시 순위
//   node pipe.js <N> search <분> [난수 씨앗] [시작 csv...]   공개 시드 점수를 직접 낮추는 탐색(방향 · 대수 · 나무를 함께 바꿈)
//   node pipe.js <N> theta <csv...>             루트가 w인 나무의 경계값을 0.00001 간격으로 훑기
//   node pipe.js <N> safety <csv> [시드0=900001] [개수=1000]   새 시드 반려 건수와 평균
//   node pipe.js <N> eval <csv...>              공개 점수와 평균 대기 · 긴급 95분위
// 목적함수는 공개 시드 점수 그 자체다(11/23 수업: Case 2~5는 공개 시드 5개로 채점).
const fs = require('fs'), path = require('path');
const { Engine, loadCase, strong, csvToSpec, specToCsv, mean, round2 } = require('./gx.js');

const [CN, CMD, ...ARGS] = process.argv.slice(2);
const tagOf = c => /\.json$/.test(c) ? path.basename(c, '.json').replace(/[^A-Za-z0-9]/g, '') : 'case' + c;
const OUT = path.join(__dirname, tagOf(CN));
fs.mkdirSync(OUT, { recursive: true });
const P = loadCase(CN), M = P.edges.length, ND = P.pd.length, Z0 = P.z0.join('');
const SEED0 = P.seeds[0], NSEED = P.seeds.length;
const logf = path.join(OUT, `${CMD}.log`);
const log = s => { console.log(s); fs.appendFileSync(logf, s + '\n'); };

// ── 방향 → 스테이션 사이 거리(웹 agvNet_과 같은 플로이드-워셜) ──
const ddCache = new Map();
function stationDist(bits) {
  if (ddCache.has(bits)) return ddCache.get(bits);
  const N = P.nodes.length, D = Array.from({ length: N }, (_, i) => { const r = new Float64Array(N).fill(Infinity); r[i] = 0; return r; });
  P.edges.forEach((e, k) => { const a = bits[k] === '1' ? e[1] : e[0], b = bits[k] === '1' ? e[0] : e[1]; D[a][b] = e[2]; });
  for (let k = 0; k < N; k++) for (let i = 0; i < N; i++) { const dik = D[i][k]; if (dik === Infinity) continue; for (let j = 0; j < N; j++) { const d = dik + D[k][j]; if (d < D[i][j]) D[i][j] = d; } }
  const set = new Set([0]); let fd = 0;
  for (const a of P.pd) for (const b of P.pd) set.add(D[a][b]);
  for (const f of P.flows) fd += f[2] * D[P.pd[f[0]]][P.pd[f[1]]];
  const r = { vals: [...set].filter(Number.isFinite).sort((a, b) => a - b), fd };
  if (ddCache.size > 5000) ddCache.clear();
  ddCache.set(bits, r); return r;
}

// ── 나무(중첩 객체 {f,d,th,lo,hi}) ──
const FEATS = ['urg', 'w', 'de', 'dl', 'qo'];
const WGRID = [0, 0.001, 0.02, 0.05, 0.1, 0.2, 0.3, 0.5, 0.75, 1, 1.25, 1.5, 2, 2.5, 3, 3.5, 4, 5, 6.5, 8];
const QGRID = [0, 1, 2, 3, 4, 6];
const N1 = (f, d, th, lo = null, hi = null) => ({ f, d, th, lo, hi });
function toTree(spec) {
  const m = {};
  for (const s of spec.nodes.split(';')) { const [id, f, d, th, lo, hi] = s.split(','); m[id] = { f, d, th: +th, lo: +lo, hi: +hi }; }
  const build = id => id && m[id] ? { f: m[id].f, d: m[id].d, th: m[id].th, lo: build(m[id].lo), hi: build(m[id].hi) } : null;
  return { dir: spec.dir, n: spec.n, root: build(1) };
}
function toSpec(t) {
  const out = []; let next = 1; const q = [[t.root, next++]];
  while (q.length) {
    const [nd, id] = q.shift();
    const lo = nd.lo ? next++ : 0, hi = nd.hi ? next++ : 0;
    out.push(`${id},${nd.f},${nd.d},${nd.th},${lo},${hi}`);
    if (nd.lo) q.push([nd.lo, lo]); if (nd.hi) q.push([nd.hi, hi]);
  }
  return { dir: t.dir, n: t.n, nodes: out.join(';') };
}
const clone = t => JSON.parse(JSON.stringify(t));
function walk(nd, anc, fn, parent, side) { if (!nd) return; fn(nd, anc, parent, side); walk(nd.lo, anc.concat(nd.f), fn, nd, 'lo'); walk(nd.hi, anc.concat(nd.f), fn, nd, 'hi'); }
const count = nd => nd ? 1 + count(nd.lo) + count(nd.hi) : 0;
function depth(nd) { return nd ? 1 + Math.max(depth(nd.lo), depth(nd.hi)) : 0; }
function valid(t) {
  if (!t.root || count(t.root) > P.maxNodes || depth(t.root) > 8) return false; // 웹 agvKey_는 8단계까지만 본다
  let ok = true; walk(t.root, [], (nd, anc) => { if (anc.includes(nd.f)) ok = false; });
  return ok && t.n >= P.nMin && t.n <= P.nMax;
}
function show(nd) { if (!nd) return '-'; const s = `${nd.f}(${nd.d},${nd.th})`; return nd.lo || nd.hi ? `${s}[${show(nd.lo)},${show(nd.hi)}]` : s; }
const keyOf = t => { const s = toSpec(t); return s.dir + '|' + s.n + '|' + s.nodes; };
function makeRng(seed) { let s = seed | 0; return () => { s = (s + 0x6D2B79F5) | 0; let t = Math.imul(s ^ (s >>> 15), 1 | s); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }; }

function mutator(rnd) {
  const pick = a => a[Math.floor(rnd() * a.length)];
  const grid = (f, dir) => f === 'urg' ? [0.5] : f === 'w' ? WGRID : f === 'qo' ? QGRID : stationDist(dir).vals;
  const randNode = (anc, dir) => { const fs2 = FEATS.filter(f => !anc.includes(f)); if (!fs2.length) return null; const f = pick(fs2); return { f, d: pick(['min', 'max']), th: pick(grid(f, dir)), lo: null, hi: null }; };
  function treeOp(t) {
    const nodes = []; walk(t.root, [], (nd, anc, parent, side) => nodes.push({ nd, anc, parent, side }));
    const x = pick(nodes), nd = x.nd, k = rnd();
    if (k < 0.30) { const g = grid(nd.f, t.dir); let i = g.indexOf(nd.th); if (i < 0) i = g.findIndex(v => v >= nd.th); nd.th = i >= 0 && rnd() < 0.7 ? g[Math.max(0, Math.min(g.length - 1, i + (rnd() < 0.5 ? -1 : 1)))] : pick(g); }
    else if (k < 0.42) { const f = pick(FEATS); nd.f = f; nd.th = pick(grid(f, t.dir)); }
    else if (k < 0.50) nd.d = nd.d === 'min' ? 'max' : 'min';
    else if (k < 0.68) { const slots = []; walk(t.root, [], (n2, anc) => { for (const s of ['lo', 'hi']) if (!n2[s]) slots.push([n2, s, anc.concat(n2.f)]); }); if (slots.length) { const [p, s, anc] = pick(slots); p[s] = randNode(anc, t.dir); } }
    else if (k < 0.80) { if (x.parent) x.parent[x.side] = null; }
    else if (k < 0.86) { const s = nd.lo; nd.lo = nd.hi; nd.hi = s; }
    else if (k < 0.91) { if (x.parent) x.parent[x.side] = (rnd() < 0.5 ? nd.lo : nd.hi) || nd.lo || nd.hi; }
    else if (k < 0.96) { const nn = randNode(x.anc, t.dir); if (nn) { nn[rnd() < 0.5 ? 'lo' : 'hi'] = nd; if (x.parent) x.parent[x.side] = nn; else t.root = nn; } }
    else { const y = pick(nodes); if (y.parent && x.parent && y !== x) x.parent[x.side] = clone(y.nd); }
  }
  function flipDir(t, nflip) {
    for (let tries = 0; tries < 30; tries++) {
      const b = t.dir.split('');
      for (let j = 0; j < nflip; j++) { const k = Math.floor(rnd() * M); b[k] = b[k] === '1' ? '0' : '1'; }
      const s = b.join(''); if (s !== t.dir && strong(P, s)) { t.dir = s; return true; }
    }
    return false;
  }
  // kind: 'tree' | 'theta' | 'n' | 'dir' | 'mix'
  return (t0, elite) => {
    for (let tries = 0; tries < 40; tries++) {
      const t = clone(t0), r = rnd();
      if (r < 0.42) { const k = 1 + Math.min(3, Math.floor(-Math.log(1 - rnd()))); for (let j = 0; j < k; j++) treeOp(t); }
      else if (r < 0.57) { if (t.root.f !== 'w') continue; t.root.th = +(Math.floor(rnd() * 600) * 0.00001).toFixed(5); }
      else if (r < 0.67) t.n += rnd() < 0.5 ? -1 : 1;
      else if (r < 0.87) { if (!flipDir(t, rnd() < 0.7 ? 1 : 2)) continue; if (rnd() < 0.3) treeOp(t); }
      else if (r < 0.94 && elite && elite.length > 1) { const o = pick(elite).t; if (rnd() < 0.5) t.dir = o.dir; else { t.root = clone(o.root); if (rnd() < 0.5) t.n = o.n; } }
      else { treeOp(t); t.n += rnd() < 0.5 ? -1 : 1; }
      if (valid(t)) return t;
    }
    return null;
  };
}

// 예시 규칙(웹 화면의 「예시로 시작」과 같은 정의)
function presets(dir) {
  const { vals } = stationDist(dir), ds = [];
  // 웹: 서로 다른 스테이션 쌍 거리의 중앙값을 5 단위로 반올림(최소 5)
  const N = P.nodes.length; void N;
  const full = fullDist(dir); P.pd.forEach(a => P.pd.forEach(b => { if (a !== b) ds.push(full[a][b]); })); ds.sort((a, b) => a - b);
  const m = ds.length ? Math.max(5, Math.round(ds[Math.floor(ds.length / 2)] / 5) * 5) : 50; void vals;
  return {
    STTF: N1('de', 'min', m),
    URG: N1('urg', 'max', 0.5, N1('de', 'min', m), N1('de', 'min', m)),
    FCFS: N1('w', 'max', 3),
  };
}
function fullDist(bits) {
  const N = P.nodes.length, D = Array.from({ length: N }, (_, i) => { const r = new Array(N).fill(Infinity); r[i] = 0; return r; });
  P.edges.forEach((e, k) => { const a = bits[k] === '1' ? e[1] : e[0], b = bits[k] === '1' ? e[0] : e[1]; D[a][b] = e[2]; });
  for (let k = 0; k < N; k++) for (let i = 0; i < N; i++) { if (D[i][k] === Infinity) continue; for (let j = 0; j < N; j++) { const d = D[i][k] + D[k][j]; if (d < D[i][j]) D[i][j] = d; } }
  return D;
}
// 방향 순위를 매길 때 쓰는 나무 묶음: 예시 규칙 + Case 1에서 배운 구조(루트 w≈0으로 도착 즉시/대기 후를 가름)
function portfolio(dir) {
  const pr = presets(dir), v = stationDist(dir).vals, far = v[Math.floor(v.length * 0.75)], mid = v[Math.floor(v.length * 0.5)];
  return {
    STTF: pr.STTF, URG: pr.URG,
    W0: N1('w', 'max', 0.001),                                                         // 도착 즉시 먼저, 나머지는 가까운 차
    W0_URG: N1('w', 'max', 0.001, null, N1('urg', 'max', 0.5)),                        // 대기 주문은 긴급 먼저
    W0_FAR: N1('w', 'max', 0.001, N1('de', 'max', far), N1('urg', 'max', 0.5)),        // 도착 즉시: 먼 차 먼저(빈 차 순환)
    W0_DEQ: N1('w', 'max', 0.001, null, N1('de', 'min', mid, N1('qo', 'max', 1), N1('urg', 'max', 0.5))),
  };
}

async function sweepN(eng, dir, root, ns, extraBlocks = 0, qcut = 150) {
  // 공개 묶음 1개 + 새 시드 묶음 extraBlocks개(순위를 덜 흔들리게)
  const jobs = [];
  for (const n of ns) { const s = toSpec({ dir, n, root }); jobs.push({ ...s, seed0: SEED0, count: NSEED, block: NSEED, qcut }); if (extraBlocks) jobs.push({ ...s, seed0: 500001, count: NSEED * extraBlocks, block: NSEED, qcut }); }
  const r = await eng.run(jobs); const out = []; let k = 0;
  for (const n of ns) { const pub = r[k++][0]; let ex = null; if (extraBlocks) { const b = r[k++]; ex = b.every(Number.isFinite) ? mean(b) : Infinity; } out.push({ n, pub, ex }); }
  return out;
}

const CMDS = {
  async base() {
    const eng = new Engine(CN), pr = presets(Z0), ns = []; for (let n = P.nMin; n <= P.nMax; n++) ns.push(n);
    log(`[기준선] 케이스 ${CN}: 노드 ${P.nodes.length}, 통로 ${M}, 스테이션 ${ND}, 공개 시드 ${P.seeds.join(',')}, 대수 ${P.nMin}~${P.nMax}, 초기 방향 Σf·d ${stationDist(Z0).fd}`);
    const res = {};
    for (const [name, root] of Object.entries(pr)) {
      const r = await sweepN(eng, Z0, root, ns, 0, 0), ok = r.filter(x => Number.isFinite(x.pub));
      const best = ok.length ? ok.reduce((a, b) => a.pub <= b.pub ? a : b) : null;
      res[name] = { tree: show(root), best: best && { n: best.n, pub: round2(best.pub) }, all: r.map(x => [x.n, Number.isFinite(x.pub) ? round2(x.pub) : null]) };
      log(`  ${name} ${show(root)}: ${best ? `최저 ${round2(best.pub)} (${best.n}대)` : '모든 대수에서 반려'} | 안정 대수 ${ok.length ? ok[0].n + '~' : '없음'}`);
      if (best) fs.writeFileSync(path.join(OUT, `base_${name}.csv`), specToCsv(toSpec({ dir: Z0, n: best.n, root })));
    }
    fs.writeFileSync(path.join(OUT, 'base.json'), JSON.stringify(res, null, 1)); eng.close();
  },

  async dirrank() {
    const top = +(ARGS[0] || 8), eng = new Engine(CN), t0 = Date.now(), srcName = ARGS[1] || 'dirs.json', sfx = srcName.replace(/^dirs/, '').replace(/\.json$/, '');
    const src = JSON.parse(fs.readFileSync(path.join(OUT, srcName), 'utf8'));
    const cands = src.cands.filter(c => strong(P, c.dir));
    log(`[방향 순위] 후보 ${cands.length}개(z0 포함), 나무 묶음 6개, 공개 묶음 + 새 시드 묶음 2개`);
    // 1단계: 대수를 성기게(3대 간격) 훑어 방향마다 좋은 대수 대역을 찾고, 2단계에서 그 주변을 촘촘히 본다
    const coarse = []; for (let n = P.nMin + 1; n <= P.nMax; n += 3) coarse.push(n);
    const rows = [];
    for (const c of cands) {
      const pf = portfolio(c.dir); let best = null;
      for (const [name, root] of Object.entries(pf)) {
        const r1 = (await sweepN(eng, c.dir, root, coarse, 0)).filter(x => Number.isFinite(x.pub)); if (!r1.length) continue;
        const b1 = r1.reduce((a, b) => a.pub <= b.pub ? a : b), fine = []; for (let n = Math.max(P.nMin, b1.n - 2); n <= Math.min(P.nMax, b1.n + 2); n++) fine.push(n);
        const r2 = (await sweepN(eng, c.dir, root, fine, 2)).filter(x => Number.isFinite(x.pub) && Number.isFinite(x.ex));
        for (const x of r2) { const sc = (x.pub + 2 * x.ex) / 3; if (!best || sc < best.score) best = { score: sc, pub: x.pub, n: x.n, tree: name, root }; }
      }
      rows.push({ ...c, best }); if (rows.length % 10 === 0) log(`  ${rows.length}/${cands.length} (${((Date.now() - t0) / 60000).toFixed(1)}분)`);
    }
    const ok = rows.filter(r => r.best).sort((a, b) => a.best.score - b.best.score);
    log(`  순위(3묶음 평균 · 공개 · 대수 · 나무 · Σf·d · 빈 차 하한 · 바꾼 통로 · 출처):`);
    ok.slice(0, 15).forEach((r, i) => log(`  ${i + 1}. ${r.best.score.toFixed(1)} · ${round2(r.best.pub)} · ${r.best.n}대 · ${r.best.tree} · ${r.sumFD} · ${r.emptyLB} · ${r.chg} · ${r.variant} | ${r.dir}`));
    const z = ok.find(r => r.variant === 'z0'); if (z) log(`  z0: ${z.best.score.toFixed(1)} · 공개 ${round2(z.best.pub)} · ${z.best.n}대 · ${z.best.tree} (순위 ${ok.indexOf(z) + 1}/${ok.length})`);
    fs.writeFileSync(path.join(OUT, `dirrank${sfx}.json`), JSON.stringify(ok.map(r => ({ dir: r.dir, variant: r.variant, sumFD: r.sumFD, emptyLB: r.emptyLB, chg: r.chg, score: +r.best.score.toFixed(3), pub: round2(r.best.pub), n: r.best.n, tree: r.best.tree })), null, 1));
    ok.slice(0, top).forEach((r, i) => fs.writeFileSync(path.join(OUT, `dirtop${sfx}_${i + 1}.csv`), specToCsv(toSpec({ dir: r.dir, n: r.best.n, root: r.best.root }))));
    log(`  상위 ${Math.min(top, ok.length)}개를 dirtop_*.csv로 저장 (${((Date.now() - t0) / 60000).toFixed(1)}분)`); eng.close();
  },

  async search() {
    const minutes = +(ARGS[0] || 20), seed = +(ARGS[1] || 1), starts = ARGS.slice(2), LAMBDA = 560, ELITE = 24, QCUT = 150;
    const rnd = makeRng(seed), mut = mutator(rnd), eng = new Engine(CN), prefix = path.join(OUT, `search_s${seed}`);
    const files = starts.length ? starts : fs.readdirSync(OUT).filter(f => /^dirtop_\d+\.csv$/.test(f)).map(f => path.join(OUT, f));
    let elite = files.map(f => ({ t: toTree(csvToSpec(fs.readFileSync(f, 'utf8'))) })).filter(e => valid(e.t));
    const seen = new Set(elite.map(e => keyOf(e.t)));
    const J = t => ({ ...toSpec(t), seed0: SEED0, count: NSEED, block: NSEED, qcut: QCUT });
    (await eng.run(elite.map(e => J(e.t)))).forEach((r, i) => { elite[i].pub = r ? r[0] : Infinity; });
    elite = elite.filter(e => Number.isFinite(e.pub)).sort((a, b) => a.pub - b.pub);
    if (!elite.length) { log('시작안이 모두 반려됨'); eng.close(); return; }
    const t0 = Date.now(), el = () => ((Date.now() - t0) / 60000).toFixed(1);
    log(`[탐색] 씨앗 ${seed}, ${minutes}분, 시작안 ${elite.length}개, 최저 공개 ${elite[0].pub.toFixed(2)} | n${elite[0].t.n} ${show(elite[0].t.root)}`);
    let gen = 0, evals = 0, last = elite[0].pub;
    const save = () => { fs.writeFileSync(prefix + '_best.csv', specToCsv(toSpec(elite[0].t))); fs.writeFileSync(prefix + '_elite.json', JSON.stringify(elite.map(e => ({ pub: e.pub, spec: toSpec(e.t), show: show(e.t.root) })), null, 1)); };
    while ((Date.now() - t0) / 60000 < minutes) {
      gen++; const pool = [];
      for (let tries = 0; pool.length < LAMBDA && tries < LAMBDA * 20; tries++) {
        const par = rnd() < 0.45 ? elite[0].t : elite[Math.floor(rnd() * rnd() * elite.length)].t;
        const t = mut(par, elite); if (!t) continue; const k = keyOf(t); if (seen.has(k)) continue; seen.add(k); pool.push({ t });
      }
      if (!pool.length) break;
      const r = await eng.run(pool.map(c => J(c.t))); evals += pool.length;
      pool.forEach((c, i) => { c.pub = r[i] ? r[i][0] : Infinity; });
      const okp = pool.filter(c => Number.isFinite(c.pub));
      if (gen === 1) log(`  1세대 점검: 후보 ${pool.length}개 중 안정 ${okp.length}개, 서로 다른 공개 점수 ${new Set(okp.map(c => c.pub.toFixed(6))).size}개`);
      // 엘리트: 점수가 같은 것은 하나만, 같은 방향은 최대 6개(방향 다양성 유지)
      const have = new Set(elite.map(e => e.pub.toFixed(6)));
      for (const c of okp) if (!have.has(c.pub.toFixed(6))) { elite.push(c); have.add(c.pub.toFixed(6)); }
      elite.sort((a, b) => a.pub - b.pub);
      const per = new Map(); elite = elite.filter(e => { const k = e.t.dir, c = per.get(k) || 0; if (c >= 6) return false; per.set(k, c + 1); return true; }).slice(0, ELITE);
      if (elite[0].pub < last - 1e-9) { log(`  세대 ${gen} (${el()}분, 평가 ${evals}): 공개 ${last.toFixed(2)} → ${elite[0].pub.toFixed(2)} | n${elite[0].t.n} ${show(elite[0].t.root)} | ${elite[0].t.dir}`); last = elite[0].pub; save(); }
      else if (gen % 20 === 0) log(`  세대 ${gen} (${el()}분, 평가 ${evals}): 최저 ${last.toFixed(2)}, 엘리트 방향 ${new Set(elite.map(e => e.t.dir)).size}종`);
      if (seen.size > 3e6) seen.clear();
    }
    save(); log(`[탐색 끝] ${el()}분, ${gen}세대, 평가 ${evals}개: 최저 공개 ${elite[0].pub.toFixed(2)} | n${elite[0].t.n} ${show(elite[0].t.root)} | ${elite[0].t.dir}`); eng.close();
  },

  async theta() {
    const eng = new Engine(CN), t0 = Date.now(), all = [], TH = Array.from({ length: 3001 }, (_, i) => +(i * 0.00001).toFixed(5));
    for (const f of ARGS) {
      const base = toTree(csvToSpec(fs.readFileSync(f, 'utf8'))); if (base.root.f !== 'w') { log(`${f}: 루트가 w가 아니라 건너뜀`); continue; }
      const list = TH.map(th => { const t = clone(base); t.root.th = th; return { t, th }; });
      const r = await eng.run(list.map(c => ({ ...toSpec(c.t), seed0: SEED0, count: NSEED, block: NSEED, qcut: 150 })));
      list.forEach((c, i) => { c.pub = r[i] ? r[i][0] : Infinity; });
      const ok = list.filter(c => Number.isFinite(c.pub)), d = new Set(ok.map(c => c.pub.toFixed(6))), best = ok.reduce((a, b) => a.pub <= b.pub ? a : b);
      log(`${path.basename(f)} n${base.n}: 서로 다른 값 ${d.size}개, 최저 ${best.pub.toFixed(2)} (경계값 ${best.th}), 원래 ${ok.find(c => c.th === base.root.th) ? ok.find(c => c.th === base.root.th).pub.toFixed(2) : '격자 밖'} [${((Date.now() - t0) / 60000).toFixed(1)}분]`);
      all.push(...ok);
    }
    const seen = new Set(), top = all.sort((a, b) => a.pub - b.pub).filter(c => !seen.has(c.pub.toFixed(6)) && seen.add(c.pub.toFixed(6))).slice(0, 5);
    top.forEach((c, i) => { log(`  ${i + 1}. ${c.pub.toFixed(2)} | 경계값 ${c.th} | n${c.t.n} ${show(c.t.root)}`); fs.writeFileSync(path.join(OUT, `theta_${i + 1}.csv`), specToCsv(toSpec(c.t))); });
    eng.close();
  },

  async safety() {
    const [f, s0 = '900001', cnt = '1000'] = ARGS, eng = new Engine(CN), spec = csvToSpec(fs.readFileSync(f, 'utf8'));
    const pub = (await eng.pub([spec]))[0], r = (await eng.run([{ ...spec, seed0: +s0, count: +cnt, block: NSEED, qcut: 0 }]))[0];
    const bad = r.filter(x => !Number.isFinite(x)).length, ok = r.filter(Number.isFinite), m = ok.length ? mean(ok) : NaN;
    const sd = Math.sqrt(ok.reduce((s, x) => s + (x - m) ** 2, 0) / Math.max(1, ok.length - 1));
    log(`[안전 확인] ${path.basename(f)}: 공개 ${round2(pub)} | 새 시드 ${s0}~${+s0 + +cnt - 1} (${r.length}묶음 × ${NSEED}시드): 반려 묶음 ${bad}건, 평균 ${m.toFixed(2)}, 묶음 표준편차 ${sd.toFixed(2)}, 최악 묶음 ${ok.length ? Math.max(...ok).toFixed(2) : '-'}`);
    eng.close();
  },

  async eval() {
    const eng = new Engine(CN);
    for (const f of ARGS) {
      const spec = csvToSpec(fs.readFileSync(f, 'utf8')), d = await eng.detail(spec, SEED0, NSEED);
      log(`${path.basename(f)}: ${d.stable ? `공개 ${round2(d.cost)} (대수 ${spec.n}, 평균 대기 ${d.mean.toFixed(4)}분, 긴급 95분위 ${d.u95.toFixed(4)}분, Σf·d ${d.sumFD}, 최대 대기열 ${d.maxQ})` : JSON.stringify(d)}`);
    }
    eng.close();
  },
};
if (!CMDS[CMD]) { console.log('사용: node pipe.js <N> base|dirrank|search|theta|safety|eval ...'); process.exit(1); }
CMDS[CMD]().catch(e => { console.error(e); process.exit(1); });
