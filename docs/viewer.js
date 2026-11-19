const features = {
  de: ["빈 차 이동거리", " m"], dl: ["적재 이동거리", " m"],
  qo: ["출발지 대기 건수", "건"], urg: ["긴급 여부", ""], w: ["대기 시간", "분"]
};
const files = document.getElementById("file");
const results = document.getElementById("results");
const status = document.getElementById("status");
const form = document.querySelector("form");
const submit = form.querySelector("button");
const scoreText = new Intl.NumberFormat("ko-KR", {minimumFractionDigits:2, maximumFractionDigits:2});
let worker;

function evaluate(csv) {
  worker ??= new Worker(new URL("./score-worker.js", import.meta.url), { type: "module" });
  return new Promise((resolve, reject) => {
    function done({data}) {
      worker.removeEventListener("error", failed);
      if (data.error) reject(new Error(data.error));
      else resolve(data);
    }
    function failed() {
      worker.removeEventListener("message", done);
      worker.terminate();
      worker = undefined;
      reject(new Error("공통 평가 엔진을 시작하지 못했습니다. 웹 빌드와 서버를 확인하세요."));
    }
    worker.addEventListener("message", done, {once:true});
    worker.addEventListener("error", failed, {once:true});
    worker.postMessage(csv);
  });
}

function describe(node, high) {
  if (node.feature === "urg") return high ? "긴급" : "일반";
  return `${high ? ">" : "≤"} ${node.threshold}${features[node.feature][1]}`;
}

function element(tag, content, parent) {
  const item = document.createElement(tag);
  if (content) item.textContent = content;
  parent.append(item);
  return item;
}

function draw(canvas, rule) {
  const points = [];
  const edges = [];
  const half = 70;
  const gap = 68;
  const row = 105;
  let nextLeaf = 0;
  function place(id, level) {
    if (id === 0) return {x: half + gap * nextLeaf++, y:42 + level * row, leaf:true};
    const node = rule.nodes.get(id);
    const low = place(node.low, level + 1);
    const high = place(node.high, level + 1);
    const point = {node, x:(low.x + high.x)/2, y:42 + level * row};
    points.push(point);
    for (const [target, isHigh] of [[low,false],[high,true]]) {
      edges.push({point, target, label:describe(node,isHigh), preferred:(node.mode === "max") === isHigh});
    }
    return point;
  }
  place(1,0);
  const width = 2 * half + (nextLeaf - 1) * gap;
  const height = 75 + rule.maxDepth * row;
  const ratio = Math.min(window.devicePixelRatio || 1, 2);
  canvas.width = width * ratio;
  canvas.height = height * ratio;
  canvas.style.width = width + "px";
  canvas.style.height = height + "px";
  const ctx = canvas.getContext("2d");
  ctx.scale(ratio,ratio);
  ctx.textAlign = "center";
  for (const {point,target,label,preferred} of edges) {
    const startY = point.y + 22, endY = target.y - (target.leaf ? 13 : 22);
    const midY = (startY + endY) / 2;
    ctx.strokeStyle = preferred ? "#245ee1" : "#8291a6";
    ctx.lineWidth = preferred ? 2.5 : 1.25;
    ctx.beginPath();
    ctx.moveTo(point.x,startY);
    ctx.lineTo(point.x,midY);
    ctx.lineTo(target.x,midY);
    ctx.lineTo(target.x,endY);
    ctx.stroke();
    ctx.font = "600 12px system-ui";
    const labelWidth = ctx.measureText(label).width + 12;
    const labelX = target.x;
    ctx.fillStyle = "#fff";
    ctx.fillRect(labelX-labelWidth/2,midY-10,labelWidth,20);
    ctx.fillStyle = preferred ? "#174dc4" : "#46566b";
    ctx.fillText(label,labelX,midY+4);
    if (target.leaf) {
      ctx.fillStyle = "#52637b";
      ctx.fillText("동점 처리",target.x,target.y+4);
    }
  }
  for (const {node,x,y} of points) {
    ctx.fillStyle = "#e9f0ff";
    ctx.fillRect(x-66,y-22,132,44);
    ctx.fillStyle = "#172b45";
    ctx.font = "600 13px system-ui";
    ctx.fillText(`#${node.id} ${features[node.feature][0]}`,x,y-2);
    ctx.font = "12px system-ui";
    ctx.fillText(`${node.feature} · ${node.mode}`,x,y+14);
  }
}


function renderTree(name, policy) {
  const title = element("h2",name,results);
  title.id = "current-tree";
  element("p",`차량 ${policy.fleet}대 · 규칙 ${policy.nodes.size}개 · 깊이 ${policy.maxDepth}`,results).className = "tree-meta";
  element("p","파란 가지가 우선입니다. 끝 노드에서 동점이면 빈 차 거리 → 요청 도착순 → 차량 번호 순으로 비교합니다.",results);
  const graph = element("div",null,results);
  graph.className = "graph";
  graph.tabIndex = 0;
  graph.setAttribute("aria-label","배차 트리 그림. 가로로 스크롤할 수 있습니다.");
  const canvas = element("canvas",null,graph);
  canvas.setAttribute("aria-hidden","true");
  draw(canvas,policy);
  const details = element("details",null,results);
  element("summary","분기 규칙을 텍스트로 보기",details);
  const list = element("ol",null,details);
  for (const node of [...policy.nodes.values()].sort((a,b)=>a.id-b.id)) {
    const low = node.low ? `#${node.low}` : "동점 처리";
    const high = node.high ? `#${node.high}` : "동점 처리";
    element("li",`#${node.id} ${features[node.feature][0]} · ${describe(node,false)} → ${low}, ${describe(node,true)} → ${high} · ${node.mode === "min" ? "왼쪽" : "오른쪽"} 우선`,list);
  }
}

function renderScores({ scores, average }) {
  const heading = element("h2","시드별 로컬 점수",results);
  heading.className = "scores-heading";
  element("p",average === null ? "반려된 시드가 있어 평균을 표시하지 않습니다." :
    `로컬 평균 ${scoreText.format(average)}점`,results).className = "average";
  const frame = element("div",null,results);
  frame.className = "score-scroll";
  const table = element("table",null,frame);
  const header = element("tr",null,element("thead",null,table));
  for (const label of ["시드","요청 데이터","비용 ↓","평균 대기","긴급 95백분위"]) {
    element("th",label,header).scope = "col";
  }
  const body = element("tbody",null,table);
  for (const {seed,source,rejected,cost,averageWait,urgent95} of scores) {
    const row = element("tr",null,body);
    element("th",String(seed),row).scope = "row";
    element("td",source === "reference" ? "공식 로그 복원" : "로컬 생성",row);
    element("td",rejected ? "반려" : scoreText.format(cost),row);
    element("td",rejected ? "—" : scoreText.format(averageWait) + "분",row);
    element("td",rejected ? "—" : scoreText.format(urgent95) + "분",row);
  }
}

form.addEventListener("submit", async event => {
  event.preventDefault();
  const file = files.files[0];
  if (!file) return;
  submit.disabled = true;
  results.replaceChildren();
  status.textContent = "시드 11~15를 평가하고 있습니다…";
  try {
    if (file.size > 8192) throw new Error("올바른 설계 CSV 파일인지 확인하세요.");
    const result = await evaluate(await file.text());
    const nodes = new Map(result.nodes.map(node => [node.id, node]));
    const depth = id => id ? 1 + Math.max(depth(nodes.get(id).low), depth(nodes.get(id).high)) : 0;
    renderTree(file.name, {...result, nodes, maxDepth: depth(1)});
    renderScores(result);
    status.textContent = "";
  } catch (error) {
    results.replaceChildren();
    status.textContent = error.message;
  } finally {
    submit.disabled = false;
  }
});
