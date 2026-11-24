// Run from this directory with Node.js. Input blobs are pinned by Git hash in inputs.json.
const fs = require('node:fs');
const path = require('node:path');
const inputs = require('./inputs.json');
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
const simulation = fs.readFileSync(path.join(__dirname,'jsim.cjs'),'utf8');
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
const out = ['case,fleet,team_fleet,first_seed,seeds,pooled_cost,rejected_seeds'];
for (const [index, input] of inputs.entries()) {
  const rows = input.csv.trim().split(/\r?\n/);
  const teamFleet = Number(rows[1].split(',')[1]);
  const bits = rows[0].split(',')[1];
  const sim = engine(input.p,bits);
  const nodes = rows.slice(2).map(s => s.slice(4)).join(';');
  for (const n of [teamFleet-1,teamFleet,teamFleet+1]) {
    const ds = sim.parseSpec(n,nodes);
    for (const seed of [11,1700001,1700006]) {
      let rejected = 0;
      const ws=[],wu=[];
      for (let i=0; i<5; i++) if (sim.simulate(ds,seed+i,0,ws,wu).unstable) rejected++;
      wu.sort((a,b)=>a-b);
      const p = input.p;
      const rate = p.alpha * p.flows.reduce((s,f)=>s+f[2],0);
      const mean = ws.reduce((a,b)=>a+b,0)/ws.length;
      const cost = p.cAGV*n+p.cW*rate*mean+p.cU*wu[Math.floor(0.95*(wu.length-1))];
      out.push([index+1,n,teamFleet,seed,5,rejected?'':cost.toFixed(4),rejected].join(','));
    }
  }
}
fs.writeFileSync(path.join(__dirname,'fleet-pilot.csv'),out.join('\n')+'\n');
console.log(out.join('\n'));
