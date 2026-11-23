// 일반판 C++ 엔진(agvg)이 웹 원본 JS(기말 케이스 페이지의 agvParse_ · agvEvaluate_)와 같은 값을 내는지 대조한다.
// 방향(강연결인 무작위 비트열 + z0) × 대수 × 나무(무작위) × 시드 묶음을 바꿔 가며 묶음 비용의 최대 차이를 본다.
// 웹 원본은 받아 둔 페이지 HTML에서 함수 부분만 잘라 web/engine_final.js로 만든다(리포에는 올리지 않음).
// 사용: node gx_verify.js <케이스 번호 또는 json 경로> [설계 수=40] [csv...]
const fs = require('fs'), path = require('path');
const { Engine, loadCase, strong, csvToSpec } = require('./gx.js');
const { toSpec, makeRng } = require('./race.js');

function webEngine() {
  const out = path.join(__dirname, 'web', 'engine_final.js');
  if (!fs.existsSync(out)) {
    const src = process.env.AGV_HTML || 'C:/dev/ie325-amr/web/final_user2.html';
    const html = fs.readFileSync(src, 'utf8');
    const a = html.indexOf('var AGV_FEATURES'), b = html.indexOf('var MAX_NODES');
    if (a < 0 || b < 0) throw new Error('채점 코드 위치를 못 찾음: ' + src);
    fs.mkdirSync(path.dirname(out), { recursive: true });
    fs.writeFileSync(out, html.slice(a, b) + '\nmodule.exports={agvParse_,agvEvaluate_,agvRun_,agvNet_};\n');
  }
  return require(out);
}
const specCsv = s => [`dir,${s.dir}`, `fleet,${s.n}`, ...s.nodes.split(';').map(x => 'key,' + x)].join('\n');

(async () => {
  const [c, nArg, ...files] = process.argv.slice(2);
  const P = loadCase(c), W = webEngine(), eng = new Engine(/\.json$/.test(c) ? P : c), rnd = makeRng(20261123);
  const M = P.edges.length, pick = a => a[Math.floor(rnd() * a.length)];
  const dvals = [...new Set(P.pd.flatMap(a => P.pd.map(b => 0)))]; // 자리 표시(거리 값은 방향마다 달라 아래에서 무작위 수로 뽑는다)
  const randDir = () => { for (;;) { let s = ''; for (let k = 0; k < M; k++) s += rnd() < 0.5 ? '1' : '0'; if (strong(P, s)) return s; } };
  const TH = { urg: () => 0.5, w: () => pick([0, 0.001, 0.05, 0.5, 1, 2, 3.3]), de: () => Math.round(rnd() * 300), dl: () => Math.round(rnd() * 300), qo: () => pick([0, 1, 2, 3]) };
  const randTree = (anc, depth) => {
    const fs2 = ['urg', 'w', 'de', 'dl', 'qo'].filter(f => !anc.includes(f)); const f = pick(fs2);
    const nd = { f, d: pick(['min', 'max']), th: TH[f](), lo: null, hi: null };
    if (depth < 3 && fs2.length > 1) { if (rnd() < 0.6) nd.lo = randTree(anc.concat(f), depth + 1); if (rnd() < 0.6) nd.hi = randTree(anc.concat(f), depth + 1); }
    return nd;
  };
  const count = nd => nd ? 1 + count(nd.lo) + count(nd.hi) : 0;
  const specs = [];
  for (const f of files) specs.push({ ...csvToSpec(fs.readFileSync(f, 'utf8')), tag: f });
  const z0 = P.z0.join(''), nTarget = +(nArg || 40), fixed = P.fixedDir || null;
  while (specs.length < nTarget + files.length) {
    let root; do { root = randTree([], 0); } while (count(root) > P.maxNodes);
    const n = P.nMin + Math.floor(rnd() * Math.min(P.nMax - P.nMin + 1, 30));
    specs.push({ ...toSpec({ n, root }), dir: fixed || (rnd() < 0.2 ? z0 : randDir()), tag: 'rand' + specs.length });
  }
  const S0 = P.seeds[0], BL = [[S0, P.seeds.length], [101, 3], [777777, 2]];
  let worst = 0, n = 0, nInf = 0, nFin = 0, bad = 0; const t0 = Date.now();
  const jobs = []; specs.forEach(s => BL.forEach(([sd, cnt]) => jobs.push({ ...s, seed0: sd, count: cnt, block: cnt })));
  const res = await eng.run(jobs);
  let k = 0;
  for (const s of specs) {
    const ps = W.agvParse_(P, specCsv(s));
    for (const [sd, cnt] of BL) {
      const x = res[k++][0]; let w;
      if (!ps.design) w = Infinity; // 형식 오류(강연결 아님 등)
      else { const r = W.agvEvaluate_(P, ps.design, Array.from({ length: cnt }, (_, i) => sd + i)); w = r.stable ? r.fleetCost + r.waitCost + r.urgCost : Infinity; }
      const diff = (w === Infinity && x === Infinity) ? 0 : Math.abs(w - x);
      if (w === Infinity) nInf++; else nFin++;
      if (diff > worst) worst = diff; n++;
      if (!(diff <= 1e-7)) { bad++; console.log(`다름! ${s.tag} n${s.n} dir ${s.dir} 시드 ${sd}×${cnt}: 웹 ${w} / C++ ${x}${ps.design ? '' : ' (웹 형식 오류: ' + ps.errs[0] + ')'}`); }
    }
  }
  console.log(`${specs.length}개 설계 × ${BL.length}개 묶음 = ${n}건 대조(유효 ${nFin}, 반려 ${nInf}), 다른 것 ${bad}건, 최대 차이 ${worst.toExponential(2)}, ${((Date.now() - t0) / 1000).toFixed(1)}초`);
  eng.close();
})();
