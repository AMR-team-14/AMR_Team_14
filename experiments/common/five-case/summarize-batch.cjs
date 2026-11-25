'use strict';
const fs = require('node:fs');
const path = require('node:path');
const [batch, ...dirs] = process.argv.slice(2);
if (!batch || !dirs.length) throw Error('Usage: node summarize-batch.cjs BATCH RESULT_DIR...');
const read = p => fs.readFileSync(p,'utf8').trim().split(/\r?\n/).filter(Boolean).map(JSON.parse);
const tasks = new Map();
const assigned = path.join(__dirname,'jobs');
for (const f of fs.readdirSync(assigned).filter(n=>n===batch+'.jsonl'||n.startsWith(batch+'-g'))) {
  for (const t of read(path.join(assigned,f))) {
    if (tasks.has(t.id)) throw Error('Duplicate assignment '+t.id);
    tasks.set(t.id,t);
  }
}
const results = new Map();
function visit(dir) {
  for (const item of fs.readdirSync(dir,{withFileTypes:true})) {
    const p=path.join(dir,item.name);
    if (item.isDirectory()) visit(p);
    else if (item.name===batch+'-github.jsonl'||item.name===batch+'-kaggle.jsonl') {
      for (const row of read(p)) {
        if (!tasks.has(row.id)) throw Error('Unexpected result '+row.id);
        if (tasks.get(row.id).env!==row.env) throw Error('Wrong environment '+row.id);
        const old=results.get(row.id);
        if (old && JSON.stringify(old.out)!==JSON.stringify(row.out)) throw Error('Conflicting result '+row.id);
        results.set(row.id,row);
      }
    }
  }
}
dirs.forEach(visit);
const report={batch, planned:tasks.size, returned:results.size, missing:[...tasks.keys()].filter(id=>!results.has(id)), failures:[], cases:{}};
const avg = a => a.length ? a.reduce((s,v)=>s+v,0)/a.length : null;
for(const row of results.values()){
  const t=tasks.get(row.id), o=row.out;
  const entry=report.cases[t.case]??={evaluations:0, searchEvaluations:0, fleets:{}, bestSearch:new Map()};
  if(o.status!=='done'){report.failures.push({id:row.id,error:o.error});continue;}
  if(t.type==='search'){
    entry.searchEvaluations+=o.evals;
    for(const r of o.results)if(r.score!==null&&(!entry.bestSearch.has(r.sha)||entry.bestSearch.get(r.sha).score>r.score)){
      entry.bestSearch.set(r.sha,{sha:r.sha,score:r.score,fleet:t.fleet,dir:t.dir,label:t.label});
    }
    continue;
  }
  entry.evaluations++;
  const ev=entry.fleets[t.label]??={blocks:0,rejected:0,scores:[],vehicle:[],wait:[],urgent:[]};
  ev.blocks++;
  ev.rejected+=(o.rejected??[]).length;
  if(o.score!==null){ev.scores.push(o.score); ev.vehicle.push(o.veh); ev.wait.push(o.wc); ev.urgent.push(o.uc);}
}
for(const entry of Object.values(report.cases)){
  entry.bestSearch=[...entry.bestSearch.values()].sort((a,b)=>a.score-b.score).slice(0,10);
  for(const [k,v] of Object.entries(entry.fleets)){
    const scores=v.scores.sort((a,b)=>a-b);
    entry.fleets[k]={blocks:v.blocks,rejected:v.rejected,mean:avg(scores),p95:scores[Math.floor(0.95*(scores.length-1))]??null,
      worst:scores.at(-1)??null,vehicle:avg(v.vehicle),wait:avg(v.wait),urgent:avg(v.urgent)};
  }
}
console.log(JSON.stringify(report,null,2));
