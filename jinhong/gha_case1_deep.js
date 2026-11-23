// GitHub Actions용 Case 1 심층 탐색(2 · 3차 파도). 앞 파도의 결과(gha/in/*.json)에서 최저안들을 읽어 그 주변만 깊게 판다.
//  A) 상위 구조 중 이 조각 몫: 대수 ±1까지, 루트 경계값 0~0.008을 0.000005 간격으로 훑음
//  B) 남은 시간: 전체 최저 8개에서 변이 탐색(조각마다 다른 난수)
// 사용: node gha_case1_deep.js <shard> <nshards> <분>
const fs = require('fs'), path = require('path');
const { Engine } = require('./cx.js');
const { toTree, toSpec, show, valid, mutator, makeRng } = require('./race.js');
const clone = t => JSON.parse(JSON.stringify(t));
(async () => {
  const [shard, nsh, minutes] = process.argv.slice(2).map(Number), eng = new Engine(), t0 = Date.now();
  const left = () => minutes - (Date.now() - t0) / 60000, el = () => ((Date.now() - t0) / 60000).toFixed(1) + '분';
  fs.mkdirSync('gha/out', { recursive: true });
  const best = new Map(); let evals = 0;
  const put = (pub, spec, sh) => { if (!Number.isFinite(pub)) return; const k = pub.toFixed(6); if (!best.has(k)) best.set(k, { pub, spec, show: sh }); if (best.size > 600) { const keep = [...best.entries()].sort((a, b) => a[1].pub - b[1].pub).slice(0, 300); best.clear(); keep.forEach(([k2, v]) => best.set(k2, v)); } };
  const files = []; (function walk(d) { if (!fs.existsSync(d)) return; for (const f of fs.readdirSync(d)) { const p = path.join(d, f); fs.statSync(p).isDirectory() ? walk(p) : p.endsWith('.json') && files.push(p); } })('gha/in');
  for (const f of files) for (const c of JSON.parse(fs.readFileSync(f, 'utf8')).top) put(c.pub, c.spec, c.show);
  const start = [...best.values()].sort((a, b) => a.pub - b.pub);
  if (!start.length) { console.log('앞 파도 결과가 없음'); process.exit(1); }
  console.log(`조각 ${shard}/${nsh}: 앞 파도 파일 ${files.length}개, 후보 ${start.length}개, 최저 ${start[0].pub.toFixed(2)}, ${minutes}분`);
  const save = tag => { const top = [...best.values()].sort((a, b) => a.pub - b.pub).slice(0, 100); fs.writeFileSync(`gha/out/shard_${shard}.json`, JSON.stringify({ shard, nsh, evals, tag, top }, null, 1)); console.log(`[${el()}] ${tag} 평가 ${evals} 최저 ${top[0].pub.toFixed(2)}`); };
  const J = t => ({ ...toSpec(t), seed0: 11, count: 5, block: 5, qcut: 120 });
  const evalAll = async list => { for (let i = 0; i < list.length && left() > 0; i += 2000) { const part = list.slice(i, i + 2000), r = await eng.run(part.map(c => J(c.t))); part.forEach((c, j) => put(r[j] ? r[j][0] : Infinity, toSpec(c.t), show(c.t.root))); evals += part.length; } };
  let lastSave = Date.now(); const tick = tag => { if (Date.now() - lastSave > 180000) { save(tag); lastSave = Date.now(); } };
  // A) 구조별 촘촘한 경계값 훑기
  const structs = new Map(); for (const c of start) { const t = toTree(c.spec); t.root.th = 0; const k = JSON.stringify(toSpec(t)); if (!structs.has(k)) structs.set(k, t); if (structs.size >= 240) break; }
  const TH = Array.from({ length: 1601 }, (_, i) => +(i * 0.000005).toFixed(6)); let si = 0;
  for (const t0s of structs.values()) {
    if (si++ % nsh !== shard) continue; if (left() < minutes * 0.45) break;
    for (const dn of [0, -1, 1]) { const n = t0s.n + dn; if (n < 18 || n > 23) continue; await evalAll(TH.map(th => { const t = clone(t0s); t.n = n; t.root.th = th; return { t }; })); }
    tick('A');
  }
  save('A 끝');
  // B) 변이 탐색
  const rnd = makeRng(7000 + shard * 13), mut = mutator(rnd, { fixroot: 1, oldonly: 0, nmin: 18, nmax: 23 }), seen = new Set();
  while (left() > 1) {
    const elite = [...best.values()].sort((a, b) => a.pub - b.pub).slice(0, 8).map(e => toTree(e.spec)), pool = [];
    for (let tries = 0; pool.length < 300 && tries < 6000; tries++) {
      const t = mut(elite[Math.floor(rnd() * elite.length)]); if (!t || !valid(t)) continue;
      if (rnd() < 0.6 && t.root.f === 'w') t.root.th = +(Math.floor(rnd() * 1600) * 0.000005).toFixed(6);
      const k = JSON.stringify(toSpec(t)); if (seen.has(k)) continue; seen.add(k); pool.push({ t });
    }
    if (!pool.length) break;
    await evalAll(pool); tick('B');
  }
  save('끝'); eng.close();
})();
