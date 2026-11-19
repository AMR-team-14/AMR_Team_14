const plans = {
 D0:"de", D1:"de(qo,-)", D2:"de(qo,w)", W0:"w", W1:"w(-,urg)", W2:"w(de(qo,-),-)",
 Q0:"qo", Q1:"qo(-,w)", Q2:"qo(dl,w)", U0:"urg", U1:"urg(-,w)", U2:"urg(qo,w)",
 L0:"dl", L1:"dl(urg,-)", L2:"dl(urg,qo)"
};
const fleetPlan=Array.from({length:30},(_,i)=>i+1).filter(n=>n<=10||n%2||n>=16&&n<=22);
const makeElement=(tag,txt)=>Object.assign(document.createElement(tag),{textContent:txt});
function structure(nodes){
 if(!Array.isArray(nodes)||nodes.length<1||nodes.length>9)throw Error("트리 노드 수를 확인하세요.");
 const seen=new Set();
 function visit(id){
  if(!id)return "-";
  const n=nodes.find(v=>v.id===id);
  if(!n||seen.has(id)||!["de","dl","urg","qo","w"].includes(n.feature))throw Error("트리 구조를 확인하세요.");
  seen.add(id);
  return n.feature+(n.low||n.high?"("+visit(n.low)+","+visit(n.high)+")":"");
 }
 const tree=visit(1);if(seen.size!==nodes.length)throw Error("연결되지 않은 노드가 있습니다.");return tree;
}
function renderCoverage(data,caption){
 const candidates=data.candidates;
 if(!Array.isArray(candidates))throw Error("candidates.json 형식을 확인하세요.");
 const groups=new Map(Object.values(plans).map(shape=>[shape,new Map()]));
 const fleets=new Set(fleetPlan);
 for(const c of candidates){
  const {fleet,nodes}=c.policy,shape=structure(nodes);
  if(!Number.isInteger(fleet)||fleet<1||fleet>30||!Array.isArray(c.sources)||!Array.isArray(c.seeds))throw Error("후보의 대수·출처·시드 형식을 확인하세요.");
  if(!groups.has(shape))groups.set(shape,new Map());
  const row=groups.get(shape);if(!row.has(fleet))row.set(fleet,[]);
  row.get(fleet).push(c);fleets.add(fleet);
 }
 const grid=document.querySelector("#coverage-grid"),details=document.querySelector("#coverage-detail");
 const cols=[...fleets].sort((a,b)=>a-b),head=document.createElement("thead"),tr=document.createElement("tr");
 for(const v of ["구조",...cols])tr.append(makeElement("th",v));
 head.append(tr);grid.replaceChildren(head);
 const body=document.createElement("tbody");
 for(const [shape,row] of groups){
  const id=Object.keys(plans).find(k=>plans[k]===shape);
  const line=document.createElement("tr");line.append(makeElement("th",id?id+" · "+shape:shape));
  for(const fleet of cols){
   const list=row.get(fleet)||[],td=document.createElement("td"),btn=makeElement("button",list.length||"·");
   btn.type="button";btn.classList.toggle("hit",!!list.length);
   btn.setAttribute("aria-label",shape+" "+fleet+"대 제출 "+list.length+"개");
   btn.onclick=()=>{
    const key="EXP-"+(id||shape)+"-N"+fleet;
    details.replaceChildren(makeElement("strong",key+" · 제출 "+list.length+"개 "));
    const a=makeElement("a","이 칸의 GitHub 이슈 확인");a.href="https://github.com/AMR-team-14/AMR_Team_14/issues?q="+encodeURIComponent('is:issue in:title "'+key+'"');details.append(a);
    for(const c of list){
     const rules=c.policy.nodes.map(n=>"#"+n.id+" "+n.feature+" "+n.mode+" "+n.threshold+" ["+n.low+","+n.high+"]").join(" / ");
     const scores=(c.scores||[]).map(s=>s.seed+"="+(s.rejected?"반려":s.cost)).join(", ");
     details.append(makeElement("span",c.sources.map(s=>s.owner+" · "+s.path).join(", ")+" | "+rules+" | "+(scores||("시드 "+(c.seeds.join(", ")||"결과 미입력")))));
    }
   };td.append(btn);line.append(td);
  }body.append(line);
 }
 grid.append(body);document.querySelector("#coverage-summary").textContent=caption+" · 제출 정책 "+candidates.length+"개. 숫자는 해당 골격·대수의 후보 수입니다. 괄호 안은 이하 가지, 초과 가지 순서이며 –는 비교 종료입니다.";
 details.textContent="칸을 선택하면 정확한 경계값과 담당 기록을 볼 수 있습니다.";
}
renderCoverage(JSON.parse(document.querySelector("#baseline-data").textContent),"공개 CSV 기준 · 곽민재 고유 3종");
document.querySelector("#coverage-file").onchange=async event=>{
 const file=event.target.files[0];if(!file)return;
 try{renderCoverage(JSON.parse(await file.text()),file.name)}catch(error){document.querySelector("#coverage-summary").textContent="불러오기 실패 · "+error.message}
};
