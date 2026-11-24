import { readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";

const [casePath, logPath, output] = process.argv.slice(2);
if (!casePath || !logPath || !output) {
  console.error("Usage: node tools/prepare-viewer-data.mjs <official-case.json> <seed11-log.csv> <docs-folder>");
  process.exit(1);
}
const read = path => readFileSync(path,"utf8").replace(/^\uFEFF/,"");
const data = JSON.parse(read(casePath));
const [headers,...lines] = read(logPath).trim().split(/\r?\n/);
const fields = headers.split(",");
const required = ["id","arrive","from","to","urg"];
if (required.some(field=>!fields.includes(field))) throw new Error("Unexpected official CSV header");
const rows = lines.map(line=>Object.fromEntries(line.split(",").map((value,i)=>[fields[i],value])))
  .sort((a,b)=>Number(a.id)-Number(b.id));
const flows = new Map(data.flows.map(([from,to],i)=>[[data.depts[from],data.depts[to]].join(","),i]));
let previous = 0;
const packed = rows.map((row,i)=>{
  const flow = flows.get([row.from,row.to].join(","));
  const time = Math.round(Number(row.arrive)*1000);
  if (flow===undefined || !Number.isSafeInteger(time) || time<previous ||
      Number(row.id)!==i || Number(row.urg)!==Number(data.flows[flow][1]===data.urgentDest)) {
    throw new Error("Unexpected official log row "+i);
  }
  const value = (time-previous)*data.flows.length+flow;
  previous = time;
  return value.toString(36);
});
const keys = "nodes edges pd depts flows urgentDest alpha vc ve tl tu at p T warm seeds qcap endQmax nMin nMax maxNodes fixedDir cAGV cW cU".split(" ");
const clean = Object.fromEntries(keys.map(key=>[key,data[key]]));
writeFileSync(join(output,"case.json"),JSON.stringify(clean)+"\n");
writeFileSync(join(output,"seed11.json"),JSON.stringify(packed.join(" "))+"\n");
