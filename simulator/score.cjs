// Run from this directory with Node.js. Input blobs are pinned by Git hash in inputs.json.
const fs = require('node:fs');
const { createHash } = require('node:crypto');
const path = require('node:path');
const inputs = require('./internal/inputs.json');
function network(p, bits) {
  const n = p.nodes.length;
  const D = Array.from({length:n}, (_,i) => Array.from({length:n}, (_,j) => i === j ? 0 : Infinity));
  const NX = Array.from({length:n}, (_,i) => Array.from({length:n}, (_,j) => i === j ? i : -1));
  p.edges.forEach(([u,v,d], k) => {
    if (bits[k]) [u,v] = [v,u];
    D[u][v] = d;
    NX[u][v] = v;
  });
  for (let k=0; k<n; k++) for (let i=0; i<n; i++) for (let j=0; j<n; j++) {
    const d = D[i][k] + D[k][j];
    if (d < D[i][j]) { D[i][j] = d; NX[i][j] = NX[i][k]; }
  }
  if (D.some(row => row.includes(Infinity))) throw Error('Disconnected');
  return {D,NX};
}
const simulation = fs.readFileSync(path.join(__dirname,'internal','jsim.cjs'),'utf8');
function engine(p, bits) {
  const module = {exports:{}};
  const resolve = name => {
    if (name === './web/engine_raw.js') return {DATA_AGV:{...p,fixedDir:bits}, agvNet_:network};
    if (name === 'worker_threads') return {isMainThread:true};
    throw Error(name);
  };
  Function('require','module',simulation)(resolve,module);
  return module.exports;
}
function evaluate(index,csv,seed,count,trace) {
  const input=inputs[index-1], rows=csv.trim().split(/\r?\n/);
  const n=Number(rows[1].split(',')[1]), bits=rows[0].split(',')[1];
  if(!/^dir,[01]+$/.test(rows[0]) || bits.length!==input.p.edges.length || !/^fleet,\d+$/.test(rows[1]) || n<1) throw Error('Invalid direction or fleet');
  const sim=engine(input.p,bits), ds=sim.parseSpec(n,rows.slice(2).map(s=>s.slice(4)).join(';'));
  const ws=[],wu=[], rejected=[];
  const digest = trace ? createHash('sha256') : null;
  let dispatchCount = 0;
  for (let i = 0; i < count; i++) {
    const currentSeed = seed + i;
    const onDispatch = digest ? (request, vehicle, time) => {
      digest.update(`${currentSeed},${request},${vehicle},${time}\n`);
      dispatchCount++;
    } : undefined;
    if (sim.simulate(ds, currentSeed, 0, ws, wu, onDispatch).unstable) rejected.push(currentSeed);
  }
  wu.sort((a,b)=>a-b);
  const p=input.p,rate=p.alpha*p.flows.reduce((s,f)=>s+f[2],0);
  const cost=p.cAGV*n+p.cW*rate*ws.reduce((s,v)=>s+v,0)/ws.length+p.cU*wu[Math.floor(0.95*(wu.length-1))];
  if(!rejected.length && !Number.isFinite(cost)) throw Error('Undefined pooled cost');
  return {
    case:index, fleet:n, first_seed:seed, seeds:count,
    pooled_cost:rejected.length?null:Number(cost.toFixed(4)), rejected,
    ...(digest ? { dispatch_count: dispatchCount, dispatch_sha256: digest.digest('hex') } : {})
  };
}
const [caseArg,file,start='11',length='5',flag]=process.argv.slice(2);
const index=Number(caseArg),seed=Number(start),count=Number(length);
if(!inputs[index-1] || !Number.isSafeInteger(seed) || !Number.isSafeInteger(count) || count<1) throw Error('Usage: node score.cjs CASE ANSWER.csv|--baseline FIRST_SEED COUNT');
console.log(JSON.stringify(evaluate(index,file==='--baseline'?inputs[index-1].csv:fs.readFileSync(file,'utf8'),seed,count,flag==='--trace')));
