// night — 11/23 밤샘 루프용 실험 도구(Case 4 · 5). pipe.js의 나무 · 구간 코드를 그대로 쓴다.
//   node night.js <N> wfine <csv> [반폭=0.3] [간격=0.0002]   나무의 w 노드마다 경계값을 촘촘히 훑어 공개 점수 분포를 본다
//   node night.js <N> dirnb <csv> [k=2]                      방향 이웃 전수(구간 1~k개 뒤집기, 나무 · 대수 고정)
//   node night.js <N> nb <csv>                               나무 1수 이웃 전수(노드별 특성 · 방향 · 경계값, 노드 추가 · 삭제) + 대수 ±1
// 결과: caseN/night_<명령>.log, 상위안은 caseN/nt_<명령>_<i>.csv
const fs = require('fs'), path = require('path');
const { Engine, loadCase, strong, csvToSpec, specToCsv, mean, round2 } = require('./gx.js');
const [CN, CMD, ...ARGS] = process.argv.slice(2);
const OUT = path.join(__dirname, 'case' + CN), P = loadCase(CN), SEED0 = P.seeds[0], NSEED = P.seeds.length;
const logf = path.join(OUT, `night_${CMD}.log`);
const log = s => { console.log(s); fs.appendFileSync(logf, s + '\n'); };
const TH = +(process.env.AGVX_THREADS || 13);

const CHAINS = (() => {
  const N = P.nodes.length, inc = Array.from({ length: N }, () => []), par = P.edges.map((_, k) => k);
  P.edges.forEach((e, k) => { inc[e[0]].push(k); inc[e[1]].push(k); });
  const find = x => par[x] === x ? x : (par[x] = find(par[x]));
  inc.forEach(l => { if (l.length === 2) par[find(l[0])] = find(l[1]); });
  const g = new Map(); P.edges.forEach((_, k) => { const r = find(k); if (!g.has(r)) g.set(r, []); g.get(r).push(k); });
  return [...g.values()];
})();
function toTree(spec) {
  const m = {};
  for (const s of spec.nodes.split(';')) { const [id, f, d, th, lo, hi] = s.split(','); m[id] = { f, d, th: +th, lo: +lo, hi: +hi }; }
  const build = id => id && m[id] ? { f: m[id].f, d: m[id].d, th: m[id].th, lo: build(m[id].lo), hi: build(m[id].hi) } : null;
  return { dir: spec.dir, n: spec.n, root: build(1) };
}
function toSpec(t) {
  if (DAG) return toSpecDag(t);
  const out = []; let next = 1; const q = [[t.root, next++]];
  while (q.length) {
    const [nd, id] = q.shift();
    const lo = nd.lo ? next++ : 0, hi = nd.hi ? next++ : 0;
    out.push(`${id},${nd.f},${nd.d},${nd.th},${lo},${hi}`);
    if (nd.lo) q.push([nd.lo, lo]); if (nd.hi) q.push([nd.hi, hi]);
  }
  return { dir: t.dir, n: t.n, nodes: out.join(';') };
}
// 공유 나무(11/24): 웹 검사(agvParse_)는 「서로 다른 기준 id ≤ maxNodes」와 「한 갈래에 같은 특성 한 번」만 본다. 모양이 같은 가지는 id 하나를 여러 곳에서 가리킬 수 있다.
//  NIGHT_DAG=1 이면 노드 수를 「서로 다른 가지 수」로 세고, 내보낼 때 같은 가지에 같은 id를 준다.
const DAG = process.env.NIGHT_DAG === '1';
const canon = nd => nd ? `${nd.f},${nd.d},${nd.th}[${canon(nd.lo)}|${canon(nd.hi)}]` : '-';
function uniqCount(root) { const s = new Set(); const rec = nd => { if (!nd) return; s.add(canon(nd)); rec(nd.lo); rec(nd.hi); }; rec(root); return s.size; }
function toSpecDag(t) {
  const ids = new Map([[canon(t.root), 1]]), out = [], q = [t.root]; let next = 2; const done = new Set();
  while (q.length) {
    const nd = q.shift(), c = canon(nd); if (done.has(c)) continue; done.add(c);
    const idOf = ch => { if (!ch) return 0; const k = canon(ch); if (!ids.has(k)) { ids.set(k, next++); } q.push(ch); return ids.get(k); };
    const lo = idOf(nd.lo), hi = idOf(nd.hi); out.push(`${ids.get(c)},${nd.f},${nd.d},${nd.th},${lo},${hi}`);
  }
  return { dir: t.dir, n: t.n, nodes: out.join(';') };
}
const clone = t => JSON.parse(JSON.stringify(t));
function walk(nd, anc, fn, parent, side) { if (!nd) return; fn(nd, anc, parent, side); walk(nd.lo, anc.concat(nd.f), fn, nd, 'lo'); walk(nd.hi, anc.concat(nd.f), fn, nd, 'hi'); }
const count = nd => nd ? 1 + count(nd.lo) + count(nd.hi) : 0;
const depth = nd => nd ? 1 + Math.max(depth(nd.lo), depth(nd.hi)) : 0;
function valid(t) {
  if (!t.root || (DAG ? uniqCount(t.root) : count(t.root)) > P.maxNodes || depth(t.root) > 8) return false;
  let ok = true; walk(t.root, [], (nd, anc) => { if (anc.includes(nd.f)) ok = false; });
  return ok && t.n >= P.nMin && t.n <= P.nMax;
}
function show(nd) { if (!nd) return '-'; const s = `${nd.f}(${nd.d},${nd.th})`; return nd.lo || nd.hi ? `${s}[${show(nd.lo)},${show(nd.hi)}]` : s; }
const nodesOf = t => { const a = []; walk(t.root, [], (nd, anc, parent, side) => a.push({ nd, anc, parent, side })); return a; };
function stationVals(bits) {
  const N = P.nodes.length, D = Array.from({ length: N }, (_, i) => { const r = new Float64Array(N).fill(Infinity); r[i] = 0; return r; });
  P.edges.forEach((e, k) => { const a = bits[k] === '1' ? e[1] : e[0], b = bits[k] === '1' ? e[0] : e[1]; D[a][b] = e[2]; });
  for (let k = 0; k < N; k++) for (let i = 0; i < N; i++) { const dik = D[i][k]; if (dik === Infinity) continue; for (let j = 0; j < N; j++) { const d = dik + D[k][j]; if (d < D[i][j]) D[i][j] = d; } }
  const set = new Set([0]); for (const a of P.pd) for (const b of P.pd) set.add(D[a][b]);
  return [...set].filter(Number.isFinite).sort((a, b) => a - b);
}
const J = t => ({ ...toSpec(t), seed0: SEED0, count: NSEED, block: NSEED, qcut: 150 });
async function pubs(eng, list) { const out = []; for (let i = 0; i < list.length; i += 4000) { const r = await eng.run(list.slice(i, i + 4000).map(J)); r.forEach(x => out.push(x ? x[0] : Infinity)); } return out; }
function stats(v) {
  const ok = v.filter(Number.isFinite).sort((a, b) => a - b), m = mean(ok), sd = Math.sqrt(ok.reduce((s, x) => s + (x - m) ** 2, 0) / Math.max(1, ok.length - 1));
  const q = p => ok[Math.floor(p * (ok.length - 1))];
  return `n ${v.length} 안정 ${ok.length} 서로 다른 값 ${new Set(ok.map(x => x.toFixed(6))).size} | 평균 ${m.toFixed(2)} sd ${sd.toFixed(2)} | 최저 ${ok[0].toFixed(2)} 1% ${q(0.01).toFixed(2)} 10% ${q(0.1).toFixed(2)} 중앙 ${q(0.5).toFixed(2)}`;
}
function saveTop(tag, cands, k = 5) {
  const seen = new Set(), top = cands.filter(c => Number.isFinite(c.pub)).sort((a, b) => a.pub - b.pub).filter(c => !seen.has(c.pub.toFixed(6)) && seen.add(c.pub.toFixed(6))).slice(0, k);
  top.forEach((c, i) => { log(`  ${i + 1}. ${c.pub.toFixed(3)} | n${c.t.n} ${show(c.t.root)} | ${c.t.dir}${c.note ? ' | ' + c.note : ''}`); fs.writeFileSync(path.join(OUT, `nt_${tag}_${i + 1}.csv`), specToCsv(toSpec(c.t))); });
  return top;
}
const readT = f => toTree(csvToSpec(fs.readFileSync(f, 'utf8')));

// 이웃 생성기(전수): 나무 1수 이웃, 방향 1~k구간 뒤집기 이웃
const WG0 = [0, 0.001, 0.005, 0.01, 0.02, 0.05, 0.1, 0.15, 0.2, 0.3, 0.4, 0.5, 0.6, 0.75, 0.9, 1, 1.25, 1.5, 1.75, 2, 2.5, 3, 3.5, 4, 5, 6.5, 8];
function treeNb(base, dns = [0]) {
  const vals = stationVals(base.dir), list = [], grid = ft => ft === 'urg' ? [0.5] : ft === 'w' ? WG0 : ft === 'qo' ? [0, 1, 2, 3, 4, 5, 6] : vals;
  const push = (t, note) => { if (valid(t)) list.push({ t, note }); };
  for (const dn of dns) {
    const b = clone(base); b.n += dn; const K = nodesOf(b).length;
    if (dn) push(clone(b), `대수 ${dn}`);
    for (let k = 0; k < K; k++) {
      for (const ft of ['urg', 'w', 'de', 'dl', 'qo']) for (const d of ['min', 'max']) for (const th of grid(ft)) { const t = clone(b), x = nodesOf(t)[k].nd; if (x.f === ft && x.d === d && x.th === th) continue; x.f = ft; x.d = d; x.th = th; push(t, `n${dn} 노드 ${k + 1} → ${ft}(${d},${th})`); }
      { const t = clone(b), x = nodesOf(t)[k]; if (x.parent) { x.parent[x.side] = null; push(t, `n${dn} 노드 ${k + 1} 삭제`); } }
      { const t = clone(b), x = nodesOf(t)[k]; if (x.parent) { x.parent[x.side] = x.nd.lo || x.nd.hi; push(t, `n${dn} 노드 ${k + 1} 건너뛰기`); } }
      { const t = clone(b), x = nodesOf(t)[k].nd; const s2 = x.lo; x.lo = x.hi; x.hi = s2; if (x.lo || x.hi) push(t, `n${dn} 노드 ${k + 1} 자식 맞바꿈`); }
      if (DAG) for (let j = 0; j < K; j++) if (j !== k) {   // 다른 가지를 통째로 가져온다(공유라 노드 수가 늘지 않는다): 빈 자리에 붙이기 · 이 노드를 바꾸기
        for (const s2 of ['lo', 'hi']) if (!nodesOf(b)[k].nd[s2]) { const t = clone(b); nodesOf(t)[k].nd[s2] = clone(nodesOf(b)[j].nd); push(t, `n${dn} 노드 ${k + 1}.${s2} ← 노드 ${j + 1} 가지`); }
        { const t = clone(b), x = nodesOf(t)[k]; if (x.parent) { x.parent[x.side] = clone(nodesOf(b)[j].nd); push(t, `n${dn} 노드 ${k + 1} ← 노드 ${j + 1} 가지`); } }
      }
      for (const s2 of ['lo', 'hi']) if (!nodesOf(b)[k].nd[s2]) for (const ft of ['urg', 'w', 'de', 'dl', 'qo']) for (const d of ['min', 'max']) for (const th of grid(ft)) { const t = clone(b); nodesOf(t)[k].nd[s2] = { f: ft, d, th, lo: null, hi: null }; push(t, `n${dn} 노드 ${k + 1}.${s2} + ${ft}(${d},${th})`); }
    }
  }
  return list;
}
function dirNb(base, kk = 2) {
  const C = CHAINS.length, list = [];
  const add = cs => { const b = base.dir.split(''); for (const c of cs) for (const k of CHAINS[c]) b[k] = b[k] === '1' ? '0' : '1'; const d = b.join(''); if (strong(P, d)) list.push({ t: { ...clone(base), dir: d }, note: '구간 ' + cs.join('+') }); };
  for (let a = 0; a < C; a++) { add([a]); if (kk >= 2) for (let b = a + 1; b < C; b++) { add([a, b]); if (kk >= 3) for (let c = b + 1; c < C; c++) add([a, b, c]); } }
  return list;
}
// 방향 하나의 「규칙을 맞춘 뒤 점수」(형식 밖 상한): 적재 거리 묶음별 반경 좌표 하강. dirls2가 방향 국소 탐색의 목적함수로 쓴다.
async function pfOne(eng, dir, n0, tpl, ncand) {
  const N = P.nodes.length, D = Array.from({ length: N }, (_, i) => { const r = new Float64Array(N).fill(Infinity); r[i] = 0; return r; });
  P.edges.forEach((e, k) => { const a2 = dir[k] === '1' ? e[1] : e[0], b2 = dir[k] === '1' ? e[0] : e[1]; D[a2][b2] = e[2]; });
  for (let k = 0; k < N; k++) for (let i = 0; i < N; i++) { const dik = D[i][k]; if (dik === Infinity) continue; for (let j = 0; j < N; j++) { const d = dik + D[k][j]; if (d < D[i][j]) D[i][j] = d; } }
  const DD = (x, y) => D[P.pd[x]][P.pd[y]];
  const mk = urg => { const m = new Map(); for (const [o, d, r] of P.flows) if ((d === P.urgentDest) === urg) { const dl = DD(o, d); if (!m.has(dl)) m.set(dl, { dl, flows: [], os: new Set() }); const g = m.get(dl); g.flows.push(`${P.depts[o]}→${P.depts[d]}`); g.os.add(o); } return [...m.values()].sort((a2, b2) => a2.dl - b2.dl).map(g => { const all = [...new Set([...g.os].flatMap(o => P.pd.map((_, x) => DD(x, o))))].sort((a2, b2) => a2 - b2).slice(0, -1); const pk = ncand < all.length ? [...new Set(Array.from({ length: ncand }, (_, i) => all[Math.round(i * (all.length - 1) / (ncand - 1))]))] : all; return { ...g, r: -1, cand: [-1, ...pk] }; }); };
  const GN = mk(false), GU = mk(true);
  const dlTree = G => { const rec = (lo, hi) => { if (lo === hi) return G[lo].r < 0 ? '-' : `de(max,${G[lo].r})`; const mid = (lo + hi) >> 1; return `dl(min,${G[mid].dl})[${rec(lo, mid)},${rec(mid + 1, hi)}]`; }; return rec(0, G.length - 1); };
  const build = n => ({ dir, n, root: parseShow(tpl.replace('{N}', dlTree(GN)).replace('{U}', dlTree(GU))) });
  const ns = [n0 - 1, n0, n0 + 1].filter(n => n >= P.nMin && n <= P.nMax), r0 = await pubs(eng, ns.map(build)); let bi0 = 0; r0.forEach((v, i) => { if (v < r0[bi0]) bi0 = i; });
  if (!Number.isFinite(r0[bi0])) return null;
  let n = ns[bi0], best = r0[bi0]; const sttf = best;
  for (let rd = 0; rd < 4; rd++) {
    let any = false;
    for (const g of [...GU, ...GN]) { const old = g.r, list = g.cand.filter(c => c !== old), ts = list.map(c => { g.r = c; return build(n); }); g.r = old; const r = await pubs(eng, ts); let bi = -1; r.forEach((v, i) => { if (v < best - 1e-9) { best = v; bi = i; } }); if (bi >= 0) { g.r = list[bi]; any = true; } }
    const cand = [n - 1, n + 1].filter(x => x >= P.nMin && x <= P.nMax), rn = await pubs(eng, cand.map(build)); rn.forEach((v, i) => { if (v < best - 1e-9) { best = v; n = cand[i]; any = true; } });
    if (!any) break;
  }
  return { dir, n, sttf, best, radii: [...GU.filter(g => g.r >= 0).map(g => '★' + g.flows.join(',') + '=' + g.r), ...GN.filter(g => g.r >= 0).map(g => g.flows.join(',') + '=' + g.r)].join(' | ') };
}
// show() 형식 문자열 → 나무.  예: "w(min,0.01)[urg(max,0.5)[-,de(max,20)],de(min,65)]"
function parseShow(str) {
  let i = 0;
  const node = () => {
    if (str[i] === '-') { i++; return null; }
    const m = /^([a-z]+)\((min|max),([-0-9.]+)\)/.exec(str.slice(i)); if (!m) throw new Error('나무 문자열 오류: ' + str.slice(i));
    i += m[0].length; const nd = { f: m[1], d: m[2], th: +m[3], lo: null, hi: null };
    if (str[i] === '[') { i++; nd.lo = node(); if (str[i] !== ',') throw new Error('쉼표 필요: ' + str.slice(i)); i++; nd.hi = node(); if (str[i] !== ']') throw new Error('] 필요: ' + str.slice(i)); i++; }
    return nd;
  };
  return node();
}
const CMDS = {
  // 임의 나무 평가: node night.js N ev <csv(방향 · 기본 대수)> <대수들 a,b,c 또는 -> <새 시드 묶음 수> "나무" ...   → 공개 점수(대수별) + 새 시드 평균
  async ev() {
    const [f, ns0, nb = '0', ...trees] = ARGS, eng = new Engine(CN, TH), base = readT(f), ns = ns0 === '-' ? [base.n] : ns0.split(',').map(Number);
    const list = trees.length ? trees.map(parseShow) : [base.root];
    for (const root of list) {
      const ts = ns.map(n => ({ dir: base.dir, n, root })); if (process.env.NIGHT_RELAX !== "1" && !ts.every(valid)) { log(`형식 오류: ${show(root)}`); continue; }
      const r = await pubs(eng, ts); let ex = '';
      if (+nb) { const bi = r.indexOf(Math.min(...r)), x = (await eng.run([{ ...toSpec(ts[bi]), seed0: 900001, count: NSEED * +nb, block: NSEED, qcut: 0 }]))[0]; ex = ` | 새 시드(n${ns[bi]}, ${nb}묶음) 평균 ${mean(x.filter(Number.isFinite)).toFixed(2)} 반려 ${x.filter(v => !Number.isFinite(v)).length}`; }
      log(`${ns.map((n, i) => `n${n}:${Number.isFinite(r[i]) ? r[i].toFixed(2) : '반려'}`).join(' ')}${ex} | ${show(root)}`);
    }
    eng.close();
  },
  // 흐름 묶음별 '건너뛰기 반경' 상한 진단(형식 밖): 한 갈래에 dl을 여러 번 써서 적재 거리 값마다 de(max,r)을 따로 준다.
  //  엔진은 형식을 보지 않으므로 「형식이 허락한다면 어디까지 내려가나」를 잰다. 제출용이 아니다.
  //  node night.js N perflow <csv> "<틀: {N} 일반 dl 나무, {U} 긴급 dl 나무>" "<일반 기본 잎>" "<긴급 기본 잎>"
  async perflow() {
    const [f, tpl, defN = '-', defU = '-'] = ARGS, eng = new Engine(CN, TH), base = readT(f), t0 = Date.now(), nd = P.pd.length;
    const N = P.nodes.length, D = Array.from({ length: N }, (_, i) => { const r = new Float64Array(N).fill(Infinity); r[i] = 0; return r; });
    P.edges.forEach((e, k) => { const a2 = base.dir[k] === '1' ? e[1] : e[0], b2 = base.dir[k] === '1' ? e[0] : e[1]; D[a2][b2] = e[2]; });
    for (let k = 0; k < N; k++) for (let i = 0; i < N; i++) { const dik = D[i][k]; if (dik === Infinity) continue; for (let j = 0; j < N; j++) { const d = dik + D[k][j]; if (d < D[i][j]) D[i][j] = d; } }
    const DD = (x, y) => D[P.pd[x]][P.pd[y]];
    const mkGroups = urg => { const m = new Map(); for (const [o, d, r] of P.flows) if ((d === P.urgentDest) === urg) { const dl = DD(o, d); if (!m.has(dl)) m.set(dl, { dl, flows: [], rate: 0 }); const g = m.get(dl); g.flows.push(`${P.depts[o]}→${P.depts[d]}`); g.rate += r; g.os = (g.os || new Set()).add(o); } return [...m.values()].sort((a2, b2) => a2.dl - b2.dl).map(g => ({ ...g, r: -1, cand: [-1, ...[...new Set([...g.os].flatMap(o => P.pd.map((_, x) => DD(x, o))))].sort((a2, b2) => a2 - b2).slice(0, -1)] })); };
    const GN = mkGroups(false), GU = mkGroups(true), tot = P.flows.reduce((s2, x) => s2 + x[2], 0);
    const dlTree = (G, def) => { const rec = (lo, hi) => { if (lo === hi) return G[lo].r < 0 ? def : `de(max,${G[lo].r})`; const mid = (lo + hi) >> 1; return `dl(min,${G[mid].dl})[${rec(lo, mid)},${rec(mid + 1, hi)}]`; }; return rec(0, G.length - 1); };
    const build = n => ({ dir: base.dir, n, root: parseShow(tpl.replace('{N}', dlTree(GN, defN)).replace('{U}', dlTree(GU, defU))) });
    let n = base.n, best = (await pubs(eng, [build(n)]))[0], evals = 1;
    log(`[흐름별 반경 상한] ${path.basename(f)} | 일반 묶음 ${GN.length}개 · 긴급 묶음 ${GU.length}개 | 반경 없음 = ${best.toFixed(2)} (지금 답 ${(await pubs(eng, [base]))[0].toFixed(2)})`);
    for (let rd = 0; rd < 6; rd++) {
      let any = false;
      for (const g of [...GU, ...GN]) {
        const old = g.r, list = g.cand.filter(c => c !== old), ts = list.map(c => { g.r = c; return build(n); }); g.r = old;
        const r = await pubs(eng, ts); evals += ts.length; let bi = -1; r.forEach((v, i) => { if (v < best - 1e-9) { best = v; bi = i; } });
        if (bi >= 0) { g.r = list[bi]; any = true; log(`  ${g.flows.join(',')} (dl ${g.dl}, ${(100 * g.rate / tot).toFixed(1)}%) → ${g.r}m 넘는 차 먼저: ${best.toFixed(2)}`); }
      }
      for (const dn of [-1, 1]) { const v = (await pubs(eng, [build(n + dn)]))[0]; evals++; if (v < best - 1e-9) { best = v; n += dn; any = true; log(`  대수 ${n}: ${best.toFixed(2)}`); } }
      if (!any) break;
    }
    const fin = build(n), x = (await eng.run([{ ...toSpec(fin), seed0: 900001, count: NSEED * 40, block: NSEED, qcut: 0 }, { ...toSpec(base), seed0: 900001, count: NSEED * 40, block: NSEED, qcut: 0 }]));
    log(`[끝] 형식 밖 최저 ${best.toFixed(2)} (n${n}, 평가 ${evals}, ${((Date.now() - t0) / 60000).toFixed(1)}분) | 새 시드 40묶음 평균: 형식 밖 ${mean(x[0]).toFixed(2)} · 지금 답 ${mean(x[1]).toFixed(2)}`);
    log('  반경: ' + [...GU.map(g => '★' + g.flows.join(',') + '=' + g.r), ...GN.map(g => g.flows.join(',') + '=' + g.r)].join(' | '));
    fs.writeFileSync(path.join(OUT, 'night_perflow.json'), JSON.stringify({ best, n, GU, GN: GN, show: show(fin.root) }, (k, v) => v instanceof Set ? [...v] : v)); eng.close();
  },
  // 곱 이웃: (방향 이웃 상위 × 나무 1수 이웃 전수) + (나무 이웃 상위 × 방향 이웃 전수). 좋아지면 그 자리에서 다시(최대 회전 수).
  //  node night.js N prod <csv> [방향 여유=3] [나무 여유=1] [상위 개수=30] [회전=3]
  async prod() {
    const [f, dslack = '3', tslack = '1', topk = '30', rounds = '3'] = ARGS, eng = new Engine(CN, TH), t0 = Date.now(), el = () => ((Date.now() - t0) / 60000).toFixed(1);
    let cur = readT(f), best = (await pubs(eng, [cur]))[0], evals = 0; const all = [], seen = new Set();
    const evalList = async list => { const fresh = list.filter(c => { const k = JSON.stringify(toSpec(c.t)); if (seen.has(k)) return false; seen.add(k); return true; }); const r = await pubs(eng, fresh.map(c => c.t)); fresh.forEach((c, i) => c.pub = r[i]); evals += fresh.length; return fresh.filter(c => Number.isFinite(c.pub)); };
    const uniq = (list, slack, k) => { const s2 = new Set(); return list.sort((a2, b2) => a2.pub - b2.pub).filter(c => c.pub <= best + slack && !s2.has(c.pub.toFixed(6)) && s2.add(c.pub.toFixed(6))).slice(0, k); };
    log(`[곱 이웃] ${path.basename(f)} 시작 ${best.toFixed(3)} | n${cur.n} ${show(cur.root)}`);
    for (let rd = 0; rd < +rounds; rd++) {
      const base0 = best, D1 = await evalList(dirNb(cur, 2)), T1 = await evalList(treeNb(cur, [0, -1, 1])); all.push(...D1, ...T1);
      const dTop = uniq(D1, +dslack, +topk), tTop = uniq(T1, +tslack, +topk);
      log(`  회전 ${rd + 1}: 방향 이웃 ${D1.length}개(상위 ${dTop.length}개 선택), 나무 이웃 ${T1.length}개(상위 ${tTop.length}개 선택) [${el()}분]`);
      for (const c of dTop) { const r = await evalList(treeNb(c.t, [0])); all.push(...r); const b = r.reduce((a2, b2) => a2.pub <= b2.pub ? a2 : b2, { pub: Infinity }); if (b.pub < best - 1e-9) { best = b.pub; cur = b.t; log(`    방향(${c.note}, ${c.pub.toFixed(2)}) × 나무(${b.note}) = ${best.toFixed(3)} [${el()}분, 평가 ${evals}]`); } }
      for (const c of tTop) { const r = await evalList(dirNb(c.t, 2)); all.push(...r); const b = r.reduce((a2, b2) => a2.pub <= b2.pub ? a2 : b2, { pub: Infinity }); if (b.pub < best - 1e-9) { best = b.pub; cur = b.t; log(`    나무(${c.note}, ${c.pub.toFixed(2)}) × 방향(${b.note}) = ${best.toFixed(3)} [${el()}분, 평가 ${evals}]`); } }
      log(`  회전 ${rd + 1} 끝: ${base0.toFixed(3)} → ${best.toFixed(3)} (평가 ${evals}, ${el()}분)`);
      if (best >= base0 - 1e-9) break;
    }
    saveTop('prod', all, 10); eng.close();
  },
  // 방향 전수 × 규칙 재적합(형식 밖 상한으로 선별): 방향마다 가까운 차 먼저로 대수를 잡고, 적재 거리 묶음별 반경을 좌표 하강으로 맞춘다.
  //  node night.js N dirpf <dirs 파일> "<틀>" [반경 후보 수=3] [줄 수=4]   → night_dirpf.json (점수 순)
  async dirpf() {
    const [srcName, tpl, ncand = '3', lanes = '4', nlo = '17', nhi = '26', mode = 'first'] = ARGS, t0 = Date.now(), el = () => ((Date.now() - t0) / 60000).toFixed(1);   // mode: first = 작은 반경부터 ncand개, spread = 전체 거리에서 고르게 ncand개
    const dirs = [...new Set(srcName.split('+').flatMap(sn => JSON.parse(fs.readFileSync(path.join(OUT, sn), 'utf8')).cands.map(c => c.dir)))].filter(d => strong(P, d));
    const tot = P.flows.reduce((s2, x) => s2 + x[2], 0), res = [], seenScore = new Set(); let next = 0, done = 0;
    const one = async (eng, dir) => {
      const N = P.nodes.length, D = Array.from({ length: N }, (_, i) => { const r = new Float64Array(N).fill(Infinity); r[i] = 0; return r; });
      P.edges.forEach((e, k) => { const a2 = dir[k] === '1' ? e[1] : e[0], b2 = dir[k] === '1' ? e[0] : e[1]; D[a2][b2] = e[2]; });
      for (let k = 0; k < N; k++) for (let i = 0; i < N; i++) { const dik = D[i][k]; if (dik === Infinity) continue; for (let j = 0; j < N; j++) { const d = dik + D[k][j]; if (d < D[i][j]) D[i][j] = d; } }
      const DD = (x, y) => D[P.pd[x]][P.pd[y]];
      const mk = urg => { const m = new Map(); for (const [o, d, r] of P.flows) if ((d === P.urgentDest) === urg) { const dl = DD(o, d); if (!m.has(dl)) m.set(dl, { dl, flows: [], rate: 0, os: new Set() }); const g = m.get(dl); g.flows.push(`${P.depts[o]}→${P.depts[d]}`); g.rate += r; g.os.add(o); } return [...m.values()].sort((a2, b2) => a2.dl - b2.dl).map(g => { const all = [...new Set([...g.os].flatMap(o => P.pd.map((_, x) => DD(x, o))))].sort((a2, b2) => a2 - b2).slice(0, -1); const pk = mode === 'spread' && +ncand < all.length ? [...new Set(Array.from({ length: +ncand }, (_, i) => all[Math.round(i * (all.length - 1) / (+ncand - 1))]))] : all.slice(0, +ncand); return { ...g, r: -1, cand: [-1, ...pk] }; }); };
      const GN = mk(false), GU = mk(true);
      const dlTree = G => { const rec = (lo, hi) => { if (lo === hi) return G[lo].r < 0 ? '-' : `de(max,${G[lo].r})`; const mid = (lo + hi) >> 1; return `dl(min,${G[mid].dl})[${rec(lo, mid)},${rec(mid + 1, hi)}]`; }; return rec(0, G.length - 1); };
      const build = n => ({ dir, n, root: parseShow(tpl.replace('{N}', dlTree(GN)).replace('{U}', dlTree(GU))) });
      const ns = []; for (let n = Math.max(P.nMin, +nlo); n <= Math.min(P.nMax, +nhi); n++) ns.push(n);
      const r0 = await pubs(eng, ns.map(build)); let bi0 = 0; r0.forEach((v, i) => { if (v < r0[bi0]) bi0 = i; });
      if (!Number.isFinite(r0[bi0])) return null;
      let n = ns[bi0], best = r0[bi0]; const sttf = best, key = r0.map(v => Number.isFinite(v) ? v.toFixed(5) : 'x').join(',');
      if (seenScore.has(key)) return { dup: true }; seenScore.add(key);
      for (let rd = 0; rd < 5; rd++) {
        let any = false;
        for (const g of [...GU, ...GN]) { const old = g.r, list = g.cand.filter(c => c !== old), ts = list.map(c => { g.r = c; return build(n); }); g.r = old; const r = await pubs(eng, ts); let bi = -1; r.forEach((v, i) => { if (v < best - 1e-9) { best = v; bi = i; } }); if (bi >= 0) { g.r = list[bi]; any = true; } }
        const rn = await pubs(eng, [build(n - 1), build(n + 1)].filter(valid)); const cand = [n - 1, n + 1].filter(x => x >= P.nMin && x <= P.nMax); rn.forEach((v, i) => { if (v < best - 1e-9) { best = v; n = cand[i]; any = true; } });
        if (!any) break;
      }
      return { dir, n, sttf, best, radii: [...GU.map(g => '★' + g.flows.join(',') + '=' + g.r), ...GN.filter(g => g.r >= 0).map(g => g.flows.join(',') + '=' + g.r)].join(' | '), show: show(build(n).root) };
    };
    log(`[방향 전수 × 규칙 재적합] ${srcName} 강연결 ${dirs.length}개, 반경 후보 ${ncand}개, ${lanes}줄`);
    await Promise.all(Array.from({ length: +lanes }, async () => { const eng = new Engine(CN, Math.max(1, Math.floor(TH / +lanes))); while (next < dirs.length) { const d = dirs[next++], r = await one(eng, d); done++; if (r && !r.dup) res.push(r); if (done % 50 === 0) log(`  ${done}/${dirs.length} (${el()}분) 지금까지 최저 ${Math.min(...res.map(x => x.best)).toFixed(2)}`); } eng.close(); }));
    res.sort((a2, b2) => a2.best - b2.best);
    log(`  서로 다른 방향 ${res.length}개 (${el()}분). 상위:`);
    res.slice(0, 20).forEach((r, i) => log(`  ${i + 1}. ${r.best.toFixed(2)} (가까운 차 먼저 ${r.sttf.toFixed(2)}) n${r.n} | ${r.dir} | ${r.radii}`));
    fs.writeFileSync(path.join(OUT, 'night_dirpf.json'), JSON.stringify(res, null, 1));
  },
  // 방향 국소 탐색(가장 가파른 내리막): 나무 · 대수를 고정하고 1~2구간 뒤집기 이웃 전수 → 가장 낮은 곳으로 → 반복
  //  node night.js N dirls <꼬리표> <csv> ["나무 문자열" | -] [대수 | -] [최대 걸음=12]   → nt_dirls_<꼬리표>.csv, 지나온 방향은 night_dirls_<꼬리표>.json
  async dirls() {
    const [tag, f, tstr = '-', n0 = '-', maxStep = '12'] = ARGS, eng = new Engine(CN, TH), t0 = Date.now(), el = () => ((Date.now() - t0) / 60000).toFixed(1);
    let cur = readT(f); if (tstr !== '-') cur.root = parseShow(tstr); if (n0 !== '-') cur.n = +n0;
    let best = (await pubs(eng, [cur]))[0], evals = 1; const trail = [{ pub: best, dir: cur.dir }], seen = new Map();
    log(`[방향 국소 탐색 ${tag}] ${path.basename(f)} n${cur.n} ${show(cur.root)} 시작 ${best.toFixed(3)}`);
    for (let st = 0; st < +maxStep; st++) {
      const nb = dirNb(cur, 2).filter(c => !seen.has(c.t.dir)), r = await pubs(eng, nb.map(c => c.t)); evals += nb.length; nb.forEach((c, i) => { c.pub = r[i]; seen.set(c.t.dir, r[i]); });
      // 대수 ±1도 같이 본다(상위 12개 방향에서)
      const top = nb.filter(c => Number.isFinite(c.pub)).sort((a2, b2) => a2.pub - b2.pub).slice(0, 12), ex = top.flatMap(c => [-1, 1].map(dn => ({ t: { ...clone(c.t), n: c.t.n + dn }, note: c.note + ` 대수 ${dn}` }))).filter(c => valid(c.t));
      const r2 = await pubs(eng, ex.map(c => c.t)); evals += ex.length; ex.forEach((c, i) => c.pub = r2[i]);
      const b = [...top, ...ex].reduce((a2, b2) => a2.pub <= b2.pub ? a2 : b2, { pub: Infinity });
      if (!(b.pub < best - 1e-9)) { log(`  걸음 ${st + 1}: 더 낮은 이웃 없음(이웃 최저 ${b.pub.toFixed(3)}) [${el()}분, 평가 ${evals}]`); break; }
      best = b.pub; cur = b.t; trail.push({ pub: best, dir: cur.dir, n: cur.n, note: b.note }); log(`  걸음 ${st + 1}: ${b.note} → ${best.toFixed(3)} n${cur.n} [${el()}분]`);
    }
    fs.writeFileSync(path.join(OUT, `nt_dirls_${tag}.csv`), specToCsv(toSpec(cur)));
    fs.writeFileSync(path.join(OUT, `night_dirls_${tag}.json`), JSON.stringify({ best, trail, top: [...seen.entries()].filter(x => Number.isFinite(x[1])).sort((a2, b2) => a2[1] - b2[1]).slice(0, 60) }));
    log(`[끝 ${tag}] ${best.toFixed(3)} n${cur.n} | ${cur.dir} (평가 ${evals}, ${el()}분)`); eng.close();
  },
  // 형식 안 4묶음 구조 전수: w(min,0.01)[ urg(max)[ dl(min,tn)[de>a, de>b], dl(min,tu)[de>c, de>d] ], W ]  (기준 최대 9개)
  //  일반 쪽 (tn,a,b)를 전수 → 긴급 쪽 (tu,c,d)를 전수 → 반복. 대수는 csv의 대수 ±1.
  //  node night.js N s4 <꼬리표> <csv(방향 · 대수)> ["W 나무"="urg(max,0.5)"] [반경 상한 m=110]
  async s4() {
    const [tag, f, W = 'urg(max,0.5)', cap = '110', form = 'urg', rootw = 'w(min,0.01)'] = ARGS, eng = new Engine(CN, TH), base = readT(f), t0 = Date.now(), el = () => ((Date.now() - t0) / 60000).toFixed(1);   // form: urg = 긴급/일반을 가른다, nourg = 가르지 않는다(dl 경계 하나로 전체를 둘로)
    const N = P.nodes.length, D = Array.from({ length: N }, (_, i) => { const r = new Float64Array(N).fill(Infinity); r[i] = 0; return r; });
    P.edges.forEach((e, k) => { const a2 = base.dir[k] === '1' ? e[1] : e[0], b2 = base.dir[k] === '1' ? e[0] : e[1]; D[a2][b2] = e[2]; });
    for (let k = 0; k < N; k++) for (let i = 0; i < N; i++) { const dik = D[i][k]; if (dik === Infinity) continue; for (let j = 0; j < N; j++) { const d = dik + D[k][j]; if (d < D[i][j]) D[i][j] = d; } }
    const dls = u => [...new Set(P.flows.filter(x => (x[1] === P.urgentDest) === u).map(x => D[P.pd[x[0]]][P.pd[x[1]]]))].sort((a2, b2) => a2 - b2);
    const dn = dls(false), du = dls(true), R = [-1, ...stationVals(base.dir).filter(v => v <= +cap)];
    const de = r => r < 0 ? '-' : `de(max,${r})`;
    const side = (t, a2, b2) => (a2 < 0 && b2 < 0) ? '-' : (t === null ? de(a2) : `dl(min,${t})[${de(a2)},${de(b2)}]`);   // t === null: 묶음 하나(반경 a)
    const dall = [...new Set([...dn, ...du])].sort((a2, b2) => a2 - b2);
    const mk = S => ({ dir: base.dir, n: S.n, root: parseShow(form === 'urg' ? `${rootw}[urg(max,0.5)[${side(S.tn, S.a, S.b)},${side(S.tu, S.c, S.d)}],${W}]` : `${rootw}[${side(S.tn, S.a, S.b)},${W}]`) });
    const evalAll = async list => { const ts = list.map(mk), ok = ts.map(valid), r = await pubs(eng, ts.filter((_, i) => ok[i])); let k = 0; return ts.map((_, i) => ok[i] ? r[k++] : Infinity); };
    let cur = { n: base.n, tn: null, a: -1, b: -1, tu: null, c: -1, d: -1 }, best = (await evalAll([cur]))[0], evals = 1;
    log(`[4묶음 전수 ${tag}] 방향 ${base.dir}, 일반 dl ${dn.length}개 · 긴급 dl ${du.length}개 · 반경 후보 ${R.length}개, W=${W}, 시작 ${best.toFixed(2)}`);
    const sweep = async (name, list) => { const r = await evalAll(list); evals += list.length; let bi = -1; r.forEach((v, i) => { if (v < best - 1e-9) { best = v; bi = i; } }); if (bi >= 0) { cur = list[bi]; log(`  ${name}: ${best.toFixed(3)} | n${cur.n} ${show(mk(cur).root)} [${el()}분, 평가 ${evals}]`); } return bi >= 0; };
    for (let rd = 0; rd < 4; rd++) {
      let any = false;
      for (const n of [cur.n, cur.n - 1, cur.n + 1, cur.n + 2]) {
        any = await sweep(`일반 n${n}`, [null, ...(form === 'urg' ? dn : dall).slice(0, -1)].flatMap(tn => R.flatMap(a2 => (tn === null ? [-1] : R).map(b2 => ({ ...cur, n, tn, a: a2, b: b2 }))))) || any;
        if (form === 'urg') any = await sweep(`긴급 n${n}`, [null, ...du.slice(0, -1)].flatMap(tu => R.flatMap(c => (tu === null ? [-1] : R).map(d => ({ ...cur, n, tu, c, d }))))) || any;
      }
      if (!any) break;
    }
    const outf = path.join(OUT, `nt_s4_${tag}.csv`); fs.writeFileSync(outf, specToCsv(toSpec(mk(cur))));
    log(`[끝 ${tag}] ${best.toFixed(3)} | n${cur.n} ${show(mk(cur).root)} → ${path.basename(outf)} (평가 ${evals}, ${el()}분)`); eng.close();
  },
  // 방향 여러 개 × 형식 안 4묶음 구조(2차원 좌표 하강): night_dirpf.json에서 (형식 밖 점수 ≤ 상한) 방향만 골라 형식 안 규칙으로 다시 줄 세운다.
  //  node night.js N dirs4 [형식 밖 상한=830] [줄 수=4] ["W"="urg(max,0.5)"] [반경 상한=110]   → night_dirs4.json, 상위 nt_dirs4_<i>.csv
  async dirs4() {
    const [lim = '830', lanes = '4', W = 'urg(max,0.5)', cap = '110'] = ARGS, t0 = Date.now(), el = () => ((Date.now() - t0) / 60000).toFixed(1);
    const src = JSON.parse(fs.readFileSync(path.join(OUT, 'night_dirpf.json'), 'utf8')).filter(x => x.best <= +lim || x.sttf <= 850), res = []; let next = 0, done = 0;
    const one = async (eng, c) => {
      const dir = c.dir, N = P.nodes.length, D = Array.from({ length: N }, (_, i) => { const r = new Float64Array(N).fill(Infinity); r[i] = 0; return r; });
      P.edges.forEach((e, k) => { const a2 = dir[k] === '1' ? e[1] : e[0], b2 = dir[k] === '1' ? e[0] : e[1]; D[a2][b2] = e[2]; });
      for (let k = 0; k < N; k++) for (let i = 0; i < N; i++) { const dik = D[i][k]; if (dik === Infinity) continue; for (let j = 0; j < N; j++) { const d = dik + D[k][j]; if (d < D[i][j]) D[i][j] = d; } }
      const dls = u => [...new Set(P.flows.filter(x => (x[1] === P.urgentDest) === u).map(x => D[P.pd[x[0]]][P.pd[x[1]]]))].sort((a2, b2) => a2 - b2);
      const dn = dls(false).slice(0, -1), du = dls(true).slice(0, -1), R = [-1, ...stationVals(dir).filter(v => v <= +cap)];
      const de = r => r < 0 ? '-' : `de(max,${r})`, side = (t, a2, b2) => (a2 < 0 && b2 < 0) ? '-' : `dl(min,${t})[${de(a2)},${de(b2)}]`;
      const mk = S => ({ dir, n: S.n, root: parseShow(`w(min,0.01)[urg(max,0.5)[${side(S.tn, S.a, S.b)},${side(S.tu, S.c, S.d)}],${W}]`) });
      let gb = null;
      for (const n of [c.n - 1, c.n, c.n + 1]) {
        let cur = { n, tn: dn[dn.length >> 1], a: -1, b: -1, tu: du[du.length >> 1], c: -1, d: -1 }; if (!valid(mk(cur))) continue; let best = (await pubs(eng, [mk(cur)]))[0];
        const sweep = async list => { const r = await pubs(eng, list.map(mk)); let bi = -1; r.forEach((v, i) => { if (v < best - 1e-9) { best = v; bi = i; } }); if (bi >= 0) cur = list[bi]; return bi >= 0; };
        for (let rd = 0; rd < 3; rd++) {
          let any = false;
          any = await sweep(du.flatMap(tu => R.map(d => ({ ...cur, tu, d })))) || any;
          any = await sweep(du.flatMap(tu => R.map(c2 => ({ ...cur, tu, c: c2 })))) || any;
          any = await sweep(dn.flatMap(tn => R.map(a2 => ({ ...cur, tn, a: a2 })))) || any;
          any = await sweep(dn.flatMap(tn => R.map(b2 => ({ ...cur, tn, b: b2 })))) || any;
          if (!any) break;
        }
        if (!gb || best < gb.best) gb = { best, t: mk(cur) };
      }
      return gb && { dir, relaxed: c.best, sttf: c.sttf, best: gb.best, n: gb.t.n, show: show(gb.t.root), spec: toSpec(gb.t) };
    };
    log(`[방향 × 형식 안 4묶음] 방향 ${src.length}개, ${lanes}줄, W=${W}`);
    await Promise.all(Array.from({ length: +lanes }, async () => { const eng = new Engine(CN, Math.max(1, Math.floor(TH / +lanes))); while (next < src.length) { const c = src[next++], r = await one(eng, c); done++; if (r) res.push(r); if (done % 10 === 0) log(`  ${done}/${src.length} (${el()}분) 지금까지 최저 ${Math.min(...res.map(x => x.best)).toFixed(2)}`); } eng.close(); }));
    res.sort((a2, b2) => a2.best - b2.best);
    res.slice(0, 15).forEach((r, i) => { log(`  ${i + 1}. ${r.best.toFixed(2)} (형식 밖 ${r.relaxed.toFixed(2)}, 가까운 차 ${r.sttf.toFixed(2)}) n${r.n} | ${r.dir} | ${r.show}`); if (i < 8) fs.writeFileSync(path.join(OUT, `nt_dirs4_${i + 1}.csv`), specToCsv(r.spec)); });
    fs.writeFileSync(path.join(OUT, 'night_dirs4.json'), JSON.stringify(res, null, 1)); log(`[끝] ${el()}분`);
  },
  // 방향 후보 파일 × 나무 여러 개 × 대수 → 방향별 최저 순위.  node night.js N rank <꼬리표> <dirs 파일> <대수들 a,b,c> <csv 또는 "나무 문자열"> ...
  async rank() {
    const [tag, srcName, ns0, ...ts] = ARGS, eng = new Engine(CN, TH), ns = ns0.split(',').map(Number);
    const dirs = [...new Set(JSON.parse(fs.readFileSync(path.join(OUT, srcName), 'utf8')).cands.filter(c => strong(P, c.dir)).map(c => c.dir))];
    const roots = ts.map(x => /\.csv$/.test(x) ? readT(x).root : parseShow(x)), list = [];
    for (const dir of dirs) for (const [ti, root] of roots.entries()) for (const n of ns) { const t = { dir, n, root }; if (valid(t)) list.push({ t, ti, note: `나무 ${ti + 1}` }); }
    const r = await pubs(eng, list.map(c => c.t)); list.forEach((c, i) => c.pub = r[i]);
    const per = new Map(); for (const c of list) { if (!Number.isFinite(c.pub)) continue; const e = per.get(c.t.dir) || { dir: c.t.dir, by: {} }; if (!e.by[c.ti] || c.pub < e.by[c.ti].pub) e.by[c.ti] = c; per.set(c.t.dir, e); }
    const rows = [...per.values()].map(e => ({ ...e, best: Math.min(...Object.values(e.by).map(x => x.pub)) })).sort((a2, b2) => a2.best - b2.best);
    log(`[방향 순위 ${tag}] ${srcName} 방향 ${dirs.length}개 × 나무 ${roots.length}개 × 대수 ${ns.join(',')} = ${list.length}건`);
    rows.slice(0, 15).forEach((e, i) => log(`  ${i + 1}. ${roots.map((_, ti) => e.by[ti] ? `${e.by[ti].pub.toFixed(2)}(n${e.by[ti].t.n})` : '-').join(' · ')} | ${e.dir}`));
    fs.writeFileSync(path.join(OUT, `night_rank_${tag}.json`), JSON.stringify(rows.slice(0, 100).map(e => ({ dir: e.dir, best: e.best, by: Object.fromEntries(Object.entries(e.by).map(([k, v]) => [k, { pub: v.pub, n: v.t.n }])) }))));
    rows.slice(0, 8).forEach((e, i) => { const b = Object.values(e.by).reduce((a2, b2) => a2.pub <= b2.pub ? a2 : b2); fs.writeFileSync(path.join(OUT, `nt_rank_${tag}_${i + 1}.csv`), specToCsv(toSpec(b.t))); });
    eng.close();
  },
  // 나무 2수 이웃(가지치기): 1수 이웃 중 (지금 답 + 여유) 이하인 상위 K개마다 다시 1수 이웃 전수. 좋아지면 거기서 다시(최대 회전).
  //  node night.js N tt <csv> [여유=1.5] [K=40] [회전=3]   → nt_tt_<i>.csv
  async tt() {
    const [f, slack = '1.5', K = '40', rounds = '3'] = ARGS, eng = new Engine(CN, TH), t0 = Date.now(), el = () => ((Date.now() - t0) / 60000).toFixed(1);
    let cur = readT(f), best = (await pubs(eng, [cur]))[0], evals = 0; const all = [], seen = new Set([JSON.stringify(toSpec(cur))]);
    const evalList = async list => { const fresh = list.filter(c => { const k = JSON.stringify(toSpec(c.t)); if (seen.has(k)) return false; seen.add(k); return true; }); const r = await pubs(eng, fresh.map(c => c.t)); fresh.forEach((c, i) => c.pub = r[i]); evals += fresh.length; return fresh.filter(c => Number.isFinite(c.pub)); };
    log(`[나무 2수 이웃] ${path.basename(f)} 시작 ${best.toFixed(3)} | n${cur.n} ${show(cur.root)}`);
    for (let rd = 0; rd < +rounds; rd++) {
      const base0 = best, T1 = await evalList(treeNb(cur, [0])); all.push(...T1);
      const s2 = new Set(), top = T1.sort((a2, b2) => a2.pub - b2.pub).filter(c => c.pub <= best + +slack && !s2.has(c.pub.toFixed(6)) && s2.add(c.pub.toFixed(6))).slice(0, +K);
      log(`  회전 ${rd + 1}: 1수 이웃 ${T1.length}개 중 ${top.length}개에서 다시 [${el()}분]`);
      for (const c of top) { const r = await evalList(treeNb(c.t, [0])); all.push(...r.map(x => ({ ...x, note: c.note + ' ⊕ ' + x.note }))); const b = r.reduce((a2, b2) => a2.pub <= b2.pub ? a2 : b2, { pub: Infinity }); if (b.pub < best - 1e-9) { best = b.pub; cur = b.t; log(`    ${c.note}(${c.pub.toFixed(2)}) ⊕ ${b.note} = ${best.toFixed(3)} [${el()}분, 평가 ${evals}]`); } }
      log(`  회전 ${rd + 1} 끝: ${base0.toFixed(3)} → ${best.toFixed(3)} (평가 ${evals}, ${el()}분)`);
      if (best >= base0 - 1e-9) break;
    }
    saveTop('tt', all, 10); eng.close();
  },
  // 나무 문자열 → CSV 파일.  node night.js N mk <방향 · 대수를 줄 csv> <출력 csv> "나무" [대수]
  async mk() {
    const [f, outf, tstr, n] = ARGS, base = readT(f), t = { dir: base.dir, n: n ? +n : base.n, root: parseShow(tstr) };
    if (!valid(t)) { console.log('형식 오류(노드 수 · 갈래 중복 · 대수)'); return; }
    fs.writeFileSync(outf, specToCsv(toSpec(t))); console.log(`${outf}: 서로 다른 기준 ${uniqCount(t.root)}개(펼치면 ${count(t.root)}개)`);
  },
  // 방향 국소 탐색(목적 = 규칙을 맞춘 뒤 점수): 1구간 뒤집기 이웃 전부 + 상위 8개끼리의 2구간 → 가장 낮은 곳으로.
  //  node night.js N dirls2 <꼬리표> <csv> "<틀>" [반경 수=7] [줄=13] [걸음=8]   → night_dirls2_<꼬리표>.json (지나온 · 본 방향 상위)
  async dirls2() {
    const [tag, f, tpl, ncand = '7', lanes = '13', steps = '8'] = ARGS, t0 = Date.now(), el = () => ((Date.now() - t0) / 60000).toFixed(1), base = readT(f);
    const engs = Array.from({ length: +lanes }, () => new Engine(CN, Math.max(1, Math.floor(TH / +lanes)))), seen = new Map();
    const evalMany = async list => { let next = 0; await Promise.all(engs.map(async eng => { while (next < list.length) { const c = list[next++]; if (seen.has(c.dir)) continue; seen.set(c.dir, null); const r = await pfOne(eng, c.dir, c.n, tpl, +ncand); seen.set(c.dir, r); } })); return list.map(c => seen.get(c.dir)).filter(Boolean); };
    const flip = (dir, cs) => { const b = dir.split(''); for (const c of cs) for (const k of CHAINS[c]) b[k] = b[k] === '1' ? '0' : '1'; return b.join(''); };
    let cur = (await evalMany([{ dir: base.dir, n: base.n }]))[0]; const trail = [cur];
    log(`[방향 국소 탐색 · 규칙 재적합 ${tag}] 시작 ${cur.best.toFixed(2)} (가까운 차 ${cur.sttf.toFixed(2)}) n${cur.n} | ${cur.radii}`);
    for (let st = 0; st < +steps; st++) {
      const C = CHAINS.length, singles = []; for (let a2 = 0; a2 < C; a2++) { const d = flip(cur.dir, [a2]); if (strong(P, d)) singles.push({ dir: d, n: cur.n, cs: [a2] }); }
      const r1 = await evalMany(singles), top = singles.map(c => ({ c, r: seen.get(c.dir) })).filter(x => x.r).sort((a2, b2) => a2.r.best - b2.r.best).slice(0, 8), pairs = [];
      for (let i = 0; i < top.length; i++) for (let j = i + 1; j < top.length; j++) { const d = flip(cur.dir, [top[i].c.cs[0], top[j].c.cs[0]]); if (strong(P, d)) pairs.push({ dir: d, n: cur.n }); }
      const r2 = await evalMany(pairs), b = [...r1, ...r2].reduce((a2, b2) => a2.best <= b2.best ? a2 : b2, { best: Infinity });
      log(`  걸음 ${st + 1}: 이웃 ${r1.length + r2.length}개 최저 ${b.best.toFixed(2)} (지금 ${cur.best.toFixed(2)}) [${el()}분]`);
      if (!(b.best < cur.best - 0.02)) break;
      cur = b; trail.push(cur); log(`    → ${cur.best.toFixed(2)} n${cur.n} | ${cur.dir} | ${cur.radii}`);
    }
    const all = [...seen.values()].filter(Boolean).sort((a2, b2) => a2.best - b2.best);
    fs.writeFileSync(path.join(OUT, `night_dirls2_${tag}.json`), JSON.stringify({ trail, top: all.slice(0, 40) }, null, 1));
    log(`[끝 ${tag}] ${cur.best.toFixed(2)} | 본 방향 ${all.length}개, 상위: ` + all.slice(0, 6).map(x => x.best.toFixed(2)).join(' ') + ` (${el()}분)`); engs.forEach(e => e.close());
  },
  // 방향 국소 탐색(형식 안 · 규칙을 방향에 맞춰 다시 고름): 이웃 방향마다 「일반 주문 중 적재 거리 상위 k묶음(또는 하위 k묶음)은 R m 넘는 차 먼저」 나무 묶음을 평가해 최저를 그 방향의 점수로 쓴다.
  //  방향이 바뀌면 dl 값이 바뀌므로 경계값을 방향마다 다시 계산한다(회전 10에서 찾은 재배치 규칙을 방향 탐색의 목적함수로).
  //  node night.js N dirls3 <꼬리표> <csv(방향 · 대수)> [걸음=4] [반경들=45,60,75,90,105,120] [k 상위=4] [k 하위=2]
  async dirls3() {
    const [tag, f, steps = '4', rs0 = '45,60,75,90,105,120', kh = '4', kl = '2'] = ARGS, eng = new Engine(CN, TH), t0 = Date.now(), el = () => ((Date.now() - t0) / 60000).toFixed(1), base = readT(f), RS = rs0.split(',').map(Number);
    const family = (dir, n) => {
      const N = P.nodes.length, D = Array.from({ length: N }, (_, i) => { const r = new Float64Array(N).fill(Infinity); r[i] = 0; return r; });
      P.edges.forEach((e, k) => { const a2 = dir[k] === '1' ? e[1] : e[0], b2 = dir[k] === '1' ? e[0] : e[1]; D[a2][b2] = e[2]; });
      for (let k = 0; k < N; k++) for (let i = 0; i < N; i++) { const dik = D[i][k]; if (dik === Infinity) continue; for (let j = 0; j < N; j++) { const d = dik + D[k][j]; if (d < D[i][j]) D[i][j] = d; } }
      const dn = [...new Set(P.flows.filter(x => x[1] !== P.urgentDest).map(x => D[P.pd[x[0]]][P.pd[x[1]]]))].sort((a2, b2) => a2 - b2), out = [];
      for (let k = 1; k <= +kh; k++) { const i = dn.length - k - 1; if (i < 0) break; for (const R of RS) out.push({ dir, n, root: parseShow(`urg(max,0.5)[dl(min,${dn[i]})[-,de(max,${R})],-]`) }); }
      for (let k = 1; k <= +kl; k++) { const i = k - 1; if (i >= dn.length - 1) break; for (const R of RS) out.push({ dir, n, root: parseShow(`urg(max,0.5)[dl(min,${dn[i]})[de(max,${R}),-],-]`) }); }
      return out;
    };
    const score = async cands => { const fam = cands.map(c => family(c.dir, c.n)), flat = fam.flat(), r = await pubs(eng, flat); let k = 0; return cands.map((c, i) => { let b = { pub: Infinity }; for (const t of fam[i]) { const v = r[k++]; if (v < b.pub) b = { pub: v, t }; } return { ...c, ...b }; }); };
    let cur = (await score([{ dir: base.dir, n: base.n, note: '시작' }]))[0], evals = 0; const seen = new Set([cur.dir]), all = [cur];
    log(`[방향 국소 탐색 · 재배치 규칙 묶음 ${tag}] 시작 ${cur.pub.toFixed(3)} n${cur.n} ${show(cur.t.root)}`);
    for (let st = 0; st < +steps; st++) {
      const nb = dirNb({ dir: cur.dir, n: cur.n, root: cur.t.root }, 2).filter(c => !seen.has(c.t.dir)).map(c => ({ dir: c.t.dir, n: cur.n, note: c.note })); nb.forEach(c => seen.add(c.dir));
      const r = (await score(nb)).filter(c => Number.isFinite(c.pub)).sort((a2, b2) => a2.pub - b2.pub); all.push(...r.slice(0, 40));
      const ex = (await score(r.slice(0, 10).flatMap(c => [-1, 1].map(dn => ({ dir: c.dir, n: c.n + dn, note: c.note + ` 대수 ${dn}` }))).filter(c => c.n >= P.nMin && c.n <= P.nMax))).filter(c => Number.isFinite(c.pub)); all.push(...ex);
      const b = [...r.slice(0, 1), ...ex].reduce((a2, b2) => a2.pub <= b2.pub ? a2 : b2, { pub: Infinity });
      if (!(b.pub < cur.pub - 1e-9)) { log(`  걸음 ${st + 1}: 더 낮은 이웃 없음(이웃 ${nb.length}개 최저 ${b.pub.toFixed(3)}) [${el()}분]`); break; }
      cur = b; log(`  걸음 ${st + 1}: ${b.note} → ${b.pub.toFixed(3)} n${b.n} ${show(b.t.root)} [${el()}분]`);
    }
    fs.writeFileSync(path.join(OUT, `nt_dirls3_${tag}.csv`), specToCsv(toSpec(cur.t)));
    const s2 = new Set(), top = all.sort((a2, b2) => a2.pub - b2.pub).filter(c => !s2.has(c.pub.toFixed(6)) && s2.add(c.pub.toFixed(6))).slice(0, 6); top.forEach((c, i) => fs.writeFileSync(path.join(OUT, `nt_dirls3_${tag}_${i + 1}.csv`), specToCsv(toSpec(c.t))));
    log(`[끝 ${tag}] ${cur.pub.toFixed(3)} n${cur.n} | ${cur.dir} | ${show(cur.t.root)} | 상위: ${top.map(c => c.pub.toFixed(2)).join(' ')} (${el()}분)`); eng.close();
  },
  // 방향 파일 전체 × 「재배치 규칙 하나」 묶음(형식 안): 방향마다 일반 주문의 적재 거리 상위 k묶음 / 하위 k묶음에 R m 넘는 차 먼저, 대수 여러 개 → 방향별 최저.
  //  node night.js N famscan <꼬리표> <dirs 파일> <대수들> [반경들] [k 상위=4] [k 하위=2] ["틀: {R} 자리에 규칙"="urg(max,0.5)[{R},-]"]
  async famscan() {
    const [tag, srcName, ns0, rs0 = '20,45,65,85,110,130,150,180', kh = '4', kl = '2', tpl = 'urg(max,0.5)[{R},-]'] = ARGS, eng = new Engine(CN, TH), t0 = Date.now(), ns = ns0.split(',').map(Number), RS = rs0.split(',').map(Number);
    const dirs = [...new Set(JSON.parse(fs.readFileSync(path.join(OUT, srcName), 'utf8')).cands.map(c => c.dir))].filter(d => strong(P, d)), list = [];
    for (const dir of dirs) {
      const N = P.nodes.length, D = Array.from({ length: N }, (_, i) => { const r = new Float64Array(N).fill(Infinity); r[i] = 0; return r; });
      P.edges.forEach((e, k) => { const a2 = dir[k] === '1' ? e[1] : e[0], b2 = dir[k] === '1' ? e[0] : e[1]; D[a2][b2] = e[2]; });
      for (let k = 0; k < N; k++) for (let i = 0; i < N; i++) { const dik = D[i][k]; if (dik === Infinity) continue; for (let j = 0; j < N; j++) { const d = dik + D[k][j]; if (d < D[i][j]) D[i][j] = d; } }
      const dn = [...new Set(P.flows.filter(x => x[1] !== P.urgentDest).map(x => D[P.pd[x[0]]][P.pd[x[1]]]))].sort((a2, b2) => a2 - b2), rules = ['-'];
      for (let k = 1; k <= +kh; k++) { const i = dn.length - k - 1; if (i < 0) break; for (const R of RS) rules.push(`dl(min,${dn[i]})[-,de(max,${R})]`); }
      for (let k = 1; k <= +kl; k++) { const i = k - 1; if (i >= dn.length - 1) break; for (const R of RS) rules.push(`dl(min,${dn[i]})[de(max,${R}),-]`); }
      for (const rule of rules) for (const n of ns) { const t = { dir, n, root: parseShow(tpl.replace('{R}', rule)) }; if (valid(t)) list.push({ t, note: rule }); }
    }
    log(`[재배치 규칙 묶음 선별 ${tag}] 방향 ${dirs.length}개, 평가 ${list.length}건`);
    const r = await pubs(eng, list.map(c => c.t)); list.forEach((c, i) => c.pub = r[i]);
    const per = new Map(); for (const c of list) if (Number.isFinite(c.pub) && (!per.has(c.t.dir) || c.pub < per.get(c.t.dir).pub)) per.set(c.t.dir, c);
    const s2 = new Set(), top = [...per.values()].sort((a2, b2) => a2.pub - b2.pub).filter(c => !s2.has(c.pub.toFixed(6)) && s2.add(c.pub.toFixed(6)));
    top.slice(0, 12).forEach((c, i) => { log(`  ${i + 1}. ${c.pub.toFixed(2)} n${c.t.n} | ${c.t.dir} | ${c.note}`); if (i < 8) fs.writeFileSync(path.join(OUT, `nt_fam_${tag}_${i + 1}.csv`), specToCsv(toSpec(c.t))); });
    log(`[끝] ${((Date.now() - t0) / 60000).toFixed(1)}분`); eng.close();
  },
  async wfine() {
    const [f, hw = '0.3', step = '0.0002'] = ARGS, eng = new Engine(CN, TH), base = readT(f), t0 = Date.now(), all = [];
    const b0 = (await pubs(eng, [base]))[0]; log(`[w 미세 훑기] ${path.basename(f)} 기준 ${b0.toFixed(3)} | ${show(base.root)}`);
    const K = nodesOf(base).length;
    for (let k = 0; k < K; k++) {
      const nd0 = nodesOf(base)[k].nd; if (nd0.f !== 'w') continue;
      const lo = Math.max(0, nd0.th - +hw), hi = nd0.th + +hw, list = [];
      for (let th = lo; th <= hi + 1e-12; th += +step) { const t = clone(base); nodesOf(t)[k].nd.th = +th.toFixed(6); list.push({ t, note: `노드 ${k + 1} th ${th.toFixed(6)}` }); }
      const r = await pubs(eng, list.map(c => c.t)); list.forEach((c, i) => c.pub = r[i]); all.push(...list);
      log(`  노드 ${k + 1} w(${nd0.d},${nd0.th}) ${lo.toFixed(4)}~${hi.toFixed(4)}: ${stats(r)} [${((Date.now() - t0) / 60000).toFixed(1)}분]`);
      // 가까운 구간(±0.05)만 따로
      const near = list.filter(c => Math.abs(nodesOf(c.t)[k].nd.th - nd0.th) <= 0.05).map(c => c.pub); if (near.length > 20) log(`    ±0.05 안: ${stats(near)}`);
    }
    saveTop('wfine', all); eng.close();
  },
  async dirnb() {
    const [f, kk = '2'] = ARGS, eng = new Engine(CN, TH), base = readT(f), t0 = Date.now(), C = CHAINS.length, list = [];
    const flip = (dir, cs) => { const b = dir.split(''); for (const c of cs) for (const k of CHAINS[c]) b[k] = b[k] === '1' ? '0' : '1'; return b.join(''); };
    const add = cs => { const d = flip(base.dir, cs); if (strong(P, d)) list.push({ t: { ...clone(base), dir: d }, note: '구간 ' + cs.join('+') }); };
    for (let a = 0; a < C; a++) { add([a]); if (+kk >= 2) for (let b = a + 1; b < C; b++) { add([a, b]); if (+kk >= 3) for (let c = b + 1; c < C; c++) add([a, b, c]); } }
    const b0 = (await pubs(eng, [base]))[0]; log(`[방향 이웃] ${path.basename(f)} 기준 ${b0.toFixed(3)}, 구간 ${C}개, 강연결 이웃 ${list.length}개(≤${kk}개 뒤집기)`);
    const r = await pubs(eng, list.map(c => c.t)); list.forEach((c, i) => c.pub = r[i]);
    log(`  ${stats(r)} [${((Date.now() - t0) / 60000).toFixed(1)}분]`);
    log(`  한 구간 뒤집기 상위: ` + list.filter(c => !c.note.includes('+') && Number.isFinite(c.pub)).sort((a, b) => a.pub - b.pub).slice(0, 8).map(c => `${c.note.slice(3)}:${c.pub.toFixed(1)}`).join(' '));
    saveTop('dirnb', list, 8); fs.writeFileSync(path.join(OUT, 'night_dirnb_all.json'), JSON.stringify(list.filter(c => Number.isFinite(c.pub)).sort((a, b) => a.pub - b.pub).slice(0, 300).map(c => ({ pub: c.pub, dir: c.t.dir, note: c.note })))); eng.close();
  },
  // 나무 1수 이웃 전수: 노드마다 (특성 × 방향 × 경계값 전부), 빈 자리마다 새 노드 전부, 노드 삭제 · 자식 맞바꾸기, 대수 ±1
  async nb() {
    const eng = new Engine(CN, TH), t0 = Date.now();
    for (const f of ARGS) {
      const base = readT(f), vals = stationVals(base.dir), list = [];
      const WG = [0, 0.001, 0.005, 0.01, 0.02, 0.05, 0.1, 0.15, 0.2, 0.3, 0.4, 0.5, 0.6, 0.75, 0.9, 1, 1.25, 1.5, 1.75, 2, 2.5, 3, 3.5, 4, 5, 6.5, 8];
      const grid = ft => ft === 'urg' ? [0.5] : ft === 'w' ? WG : ft === 'qo' ? [0, 1, 2, 3, 4, 5, 6] : vals;
      const push = (t, note) => { if (valid(t)) list.push({ t, note }); };
      for (const dn of [0, -1, 1]) {
        const b = clone(base); b.n += dn; const K = nodesOf(b).length;
        if (dn) push(clone(b), `대수 ${dn}`);
        for (let k = 0; k < K; k++) {
          for (const ft of ['urg', 'w', 'de', 'dl', 'qo']) for (const d of ['min', 'max']) for (const th of grid(ft)) { const t = clone(b), x = nodesOf(t)[k].nd; if (x.f === ft && x.d === d && x.th === th) continue; x.f = ft; x.d = d; x.th = th; push(t, `n${dn} 노드 ${k + 1} → ${ft}(${d},${th})`); }
          { const t = clone(b), x = nodesOf(t)[k]; if (x.parent) { x.parent[x.side] = null; push(t, `n${dn} 노드 ${k + 1} 삭제`); } }
          { const t = clone(b), x = nodesOf(t)[k]; if (x.parent) { x.parent[x.side] = x.nd.lo || x.nd.hi; push(t, `n${dn} 노드 ${k + 1} 건너뛰기`); } }
          { const t = clone(b), x = nodesOf(t)[k].nd; const s = x.lo; x.lo = x.hi; x.hi = s; if (x.lo || x.hi) push(t, `n${dn} 노드 ${k + 1} 자식 맞바꿈`); }
          for (const s of ['lo', 'hi']) if (!nodesOf(b)[k].nd[s]) for (const ft of ['urg', 'w', 'de', 'dl', 'qo']) for (const d of ['min', 'max']) for (const th of grid(ft)) { const t = clone(b); nodesOf(t)[k].nd[s] = { f: ft, d, th, lo: null, hi: null }; push(t, `n${dn} 노드 ${k + 1}.${s} + ${ft}(${d},${th})`); }
        }
      }
      const b0 = (await pubs(eng, [base]))[0]; const r = await pubs(eng, list.map(c => c.t)); list.forEach((c, i) => c.pub = r[i]);
      log(`[나무 이웃] ${path.basename(f)} 기준 ${b0.toFixed(3)} | ${stats(r)} [${((Date.now() - t0) / 60000).toFixed(1)}분]`);
      const tag = 'nb_' + path.basename(f, '.csv'); saveTop(tag, list, 8);
      fs.writeFileSync(path.join(OUT, `night_${tag}.json`), JSON.stringify(list.filter(c => Number.isFinite(c.pub)).sort((a, b) => a.pub - b.pub).slice(0, 200).map(c => ({ pub: c.pub, note: c.note, spec: toSpec(c.t) }))));
    }
    eng.close();
  },
};
module.exports = { parseShow, CHAINS, toTree, toSpec, clone, walk, valid, show, nodesOf, stationVals, pubs, stats, P };
if (require.main === module) {
  if (!CMDS[CMD]) { console.log('사용: node night.js <N> wfine|dirnb|nb ...'); process.exit(1); }
  CMDS[CMD]().catch(e => { console.error(e); process.exit(1); });
}
