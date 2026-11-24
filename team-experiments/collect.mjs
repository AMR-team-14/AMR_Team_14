import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import {execFileSync as run} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const root = path.dirname(fileURLToPath(import.meta.url));
const git = (...args) => run('git',args,{cwd:root,maxBuffer:256*1024*1024}).toString('utf8');
const refs = git('for-each-ref','--format=%(refname:strip=3)','refs/remotes/origin').trim().split('\n').map(x=>x.trim()).filter(x=>x && x!=='HEAD');
const rows = new Map();
const stored = new Map();
for(const b of refs){
  const sha=git('rev-parse','origin/'+b).trim();
  const entries=git('ls-tree','--full-tree','-r','-t','-z',sha).split('\0');
  for(const line of entries){
    if(!line) continue;
    const tab=line.indexOf('\t'), p=line.slice(tab+1);
    const [mode,type,id]=line.slice(0,tab).split(' ');
    if((p.startsWith('team-experiments/') && !/^team-experiments\/case[1-5]\/submissions\//.test(p)) || p.split('/').includes('.github')) continue;
    const tree=type==='tree' && p.endsWith('/trees');
    if(!tree && (type!=='blob' || mode==='120000' || p.includes('/trees/') || !/\.(csv|tsv|md|txt|jsonl?|gz|log|png|xlsx|zip|parquet|out|ya?ml|html)$/i.test(p))) continue;
    const match=p.match(/(?:case[_-]?|(?:^|\/)ans_c|(?:^|\/)c)([1-5])(?:\D|$)/i);
    const bucket=match?'case'+match[1]:/^(experiments\/(JSM|younbean)\/|answer_final_best|agv_dispatch_log_seed11)/i.test(p)?'case1':'shared';
    const key=[bucket,p,id].join('\t');
    const row=rows.get(key) || {bucket,p,id,tree,refs:[]};
    row.refs.push([b,sha]); rows.set(key,row);
    if(!tree && !stored.has(id)) stored.set(id,bucket+'/archive/'+id.slice(0,2)+'/'+id+path.extname(p));
  }
}
const tmp=fs.mkdtempSync(path.join(os.tmpdir(),'team-logs-'));
try {
  const missing=[...stored].filter(([,f])=>!fs.existsSync(path.join(root,f)));
  const pack=path.join(tmp,'blobs');
  const fd=fs.openSync(pack,'w');
  try {run('git',['cat-file','--batch'],{cwd:root,input:missing.map(([id])=>id+'\n').join(''),stdio:['pipe',fd,'inherit']});} finally {fs.closeSync(fd);}
  const input=fs.openSync(pack,'r'); let pos=0;
  try {for(const [id,f] of missing){
    const header=Buffer.alloc(100); fs.readSync(input,header,0,100,pos);
    const end=header.indexOf(10), [actual,type,size]=header.subarray(0,end).toString().split(' ');
    if(actual!==id || type!=='blob') throw Error('Invalid git record '+id);
    const data=Buffer.alloc(Number(size)); pos+=end+1;
    if(fs.readSync(input,data,0,data.length,pos)!==data.length) throw Error('Incomplete '+id);
    pos+=data.length+1;
    const target=path.join(root,f); fs.mkdirSync(path.dirname(target),{recursive:true}); fs.writeFileSync(target,data);
  }} finally {fs.closeSync(input);}
} finally {fs.rmSync(tmp,{recursive:true,force:true});}
const quote=x=>'"'+String(x).replaceAll('"','""')+'"';
for(const bucket of ['case1','case2','case3','case4','case5','shared']){
  const table=[['original_path','branches','git_object','local_file','permalink']];
  for(const r of rows.values()) if(r.bucket===bucket){
    const [branch,sha]=r.refs[0];
    table.push([r.p,r.refs.map(([b])=>b).join(' | '),r.id,r.tree?'':stored.get(r.id),'https://github.com/AMR-team-14/AMR_Team_14/'+(r.tree?'tree':'blob')+'/'+sha+'/'+r.p.split('/').map(encodeURIComponent).join('/')]);
  }
  fs.mkdirSync(path.join(root,bucket),{recursive:true});
  fs.writeFileSync(path.join(root,bucket,'inventory.csv'),table.map(row=>row.map(quote).join(',')).join('\n')+'\n');
  console.log(bucket,table.length-1);
}
console.log('Branches:',refs.join(', '),'Unique files:',stored.size);


