const fs = require('node:fs');
const L = require('../lib.cjs');
const batch = 'overnight-45';
const known = {"4":["0111111111111111011000000111110111100000010000111100000110011100011111100101","0111111111111111011010000111110111100000010000111100000110011100011111100101","0011011111111110011011000111110111100000010010111110000110011100011111100101","0011111111111111011011000111110111100000110010101110000110011100011111100101","0111111111111111011010000111110111100000010000111100001110001100011111100101","0111011111111110011010000111110111100000010001111100001110001100011111100101","0111111111111111111010001111110111100000011001101100000100011100011111100111","1111111111011111111000001110100111100000110010011101101100011110011111101111"],"5":["000111100111100001000010010011","111000010011100001000001100011","000111100011100001000010010011","111000010000011110100001101100"]};
const gh = Array.from({length:8},()=>[]);
const kg = [];
const seen = new Set();
let counter = 0;
function add(env, task) {
  if (seen.has(task.id)) throw Error('Duplicate task: ' + task.id);
  seen.add(task.id);
  (env === 'github' ? gh[counter++ % gh.length] : kg).push({...task, env, batch});
}
function evaluate(c, label, design, start, env) {
  const cand = L.candidate(c, design);
  const id = batch+':c'+c+':'+cand.sha.slice(0,16)+':'+start;
  if (seen.has(id)) return;
  add(env,{id,type:'eval',case:c,label,candSha:cand.sha,design:cand.text,start,count:5});
}
const base = c => L.parseDesign(L.caseInput(c).csv);
for (const c of [4,5]) {
  const d = base(c);
  for (const fleet of c===4?[17,18]:[22,23,24]) for (let k=0;k<200;k++) {
    evaluate(c,'published-tree-fleet-'+fleet,{...d,fleet},2450001+5*k,'github');
  }
}
// Case 4: private re-ranking of new cycles from distinct benchmark directions.
// Target the worst approach from the urgent sink to its urgent origins before tree search.
const p4 = L.caseInput(4).p;
function reverseCycle(p, bits, edge, rotation) {
  const adj = p.nodes.map(()=>[]);
  p.edges.forEach(([u,v],i) => {
    if (bits[i]==='1') [u,v]=[v,u];
    if (i!==edge) adj[u].push([v,i]);
  });
  let [source,target] = p.edges[edge];
  if (bits[edge]==='1') [source,target]=[target,source];
  const q=[target], prev=new Map([[target,null]]);
  for(let k=0;k<q.length&&!prev.has(source);k++){
    const u=q[k], edges=adj[u];
    for(let j=0;j<edges.length;j++){
      const [v,e]=edges[(j+rotation)%edges.length];
      if(!prev.has(v)){prev.set(v,[u,e]);q.push(v);}
    }
  }
  if(!prev.has(source)) return null;
  const a=bits.split('');
  const flip=i=>{a[i]=a[i]==='1'?'0':'1'};
  flip(edge);
  for(let u=source;u!==target;){const [v,e]=prev.get(u);flip(e);u=v;}
  return a.join('');
}
const anchors = [...new Set([base(4).dir,...known[4]])];
const pool = new Set();
for(const dir of anchors) for(let edge=0;edge<p4.edges.length;edge++) for(let variation=0;variation<3;variation++){
  const b=reverseCycle(p4,dir,edge,variation);
  if(b&&!anchors.includes(b)) pool.add(b);
}
const urgent = p4.flows.filter(([,t])=>t===p4.urgentDest);
const ranked = [...pool].map(dir=>{
  const net=L.network(p4,dir.split('').map(Number));
  const from=p4.pd[p4.urgentDest];
  const access=urgent.reduce((sum,[s,,f])=>sum+net.D[from][p4.pd[s]]*f,0);
  return {dir,access,distance:L.geometry(4,dir).sumFD};
});
ranked.sort((a,b)=>a.access-b.access||a.distance-b.distance||a.dir.localeCompare(b.dir));
const targets=new Set(ranked.slice(0,32).map(r=>r.dir));
ranked.sort((a,b)=>a.distance-b.distance||a.access-b.access||a.dir.localeCompare(b.dir));
for(const row of ranked) { if(targets.size>=64)break; targets.add(row.dir); }
for(const [idx,dir] of [...targets].entries()) for(const fleet of [17,18]){
  add('kaggle',{id:batch+':c4:search:'+idx+':'+fleet,type:'search',case:4,label:'c4-alternative-cycle',dir,fleet,only:'longFar',budget:200,seed:451000+idx*2+fleet});
}
// Case 5: only directions that exactly separate A-to-B normal flows, plus approximate controls.
const p5=L.caseInput(5).p;
const bridge=p5.meta.bridge.edges[0];
function normalized(dir){if(dir[bridge]==='0')return dir; const b=dir.split(''); for(const i of p5.meta.bridge.edges)b[i]=b[i]==='0'?'1':'0';return b.join('');}
const excluded=new Set([...known[5],base(5).dir].map(normalized));
const normal=p5.flows.filter(([,t])=>t!==p5.urgentDest);
const d5=[...new Set(L.directions(5).dirs.map(normalized))];
if(d5.length!==324)throw Error('Unexpected Case 5 directions: '+d5.length);
const fits=[];
for(const dir of d5){
  if(excluded.has(dir))continue;
  const net=L.network(p5,dir.split('').map(Number));
  const rows=normal.map(([s,t,f])=>({dist:net.D[p5.pd[s]][p5.pd[t]],f,target:p5.depts[s].startsWith('A-')&&p5.depts[t].startsWith('B-')}));
  let error=Infinity, threshold, high;
  for(const value of new Set(rows.map(r=>r.dist)))for(const hi of [true,false]){
    const e=rows.reduce((sum,r)=>sum+((hi?r.dist>value:r.dist<=value)!==r.target?r.f:0),0);
    if(e<error){error=e;threshold=value;high=hi;}
  }
  fits.push({dir,error,threshold,high});
}
fits.sort((a,b)=>a.error-b.error||a.dir.localeCompare(b.dir));
const exact=fits.filter(r=>r.error===0);
const chosen=[...exact,...fits.filter(r=>r.error>0).slice(0,12)];
for(const [idx,b] of chosen.entries()) for(const fleet of [22,23,24]){
  const de=L.geometry(5,b.dir).de;
  const radius=de[Math.floor(0.75*(de.length-1))];
  const nodes=[{id:1,f:'urg',mx:'max',th:0.5,lo:2,hi:0},{id:2,f:'dl',mx:'min',th:b.threshold,lo:b.high?0:3,hi:b.high?3:0},{id:3,f:'de',mx:'max',th:radius,lo:0,hi:0}];
  const transplant=L.candidate(5,{dir:b.dir,fleet,nodes}).text;
  add('kaggle',{id:batch+':c5:search:'+idx+':'+fleet,type:'search',case:5,label:'c5-return-flow-'+b.error,dir:b.dir,fleet,transplant,only:'transplant',budget:240,seed:551000+idx*3+fleet});
}
for(const [i,rows] of gh.entries())fs.writeFileSync('jobs/'+batch+'-g'+i+'.jsonl',rows.map(x=>JSON.stringify(x)).join('\n')+'\n');
fs.writeFileSync('jobs/'+batch+'.jsonl',kg.map(x=>JSON.stringify(x)).join('\n')+'\n');
console.log({github:counter,kaggle:kg.length,case4Directions:targets.size,case5ExactUnlisted:exact.length,case5Approx:chosen.length-exact.length,searchBudget:kg.reduce((n,t)=>n+t.budget,0)});
