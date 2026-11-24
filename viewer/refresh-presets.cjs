const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname,'..');
const labels = ['고정 통로','격자형 통로','루프형 통로','고밀도 교차로','두 구역의 병목'];
const presets = labels.map((label, index) => {
  const n = index + 1;
  const spec = JSON.parse(fs.readFileSync(path.join(root,'simulator','spec','c' + n + '.json'),'utf8'));
  const answer = fs.readFileSync(path.join(root,'experiments','case' + n,'best-answer.csv'),'utf8');
  return { label, answer, edges:spec.edges.length, fixedDir:spec.fixedDir, nMin:spec.nMin,
    nMax:spec.nMax, maxNodes:spec.maxNodes, paired:spec.meta?.bridge?.edges };
});
const file = path.join(__dirname,'index.html');
const html = fs.readFileSync(file,'utf8');
const pattern = /(<script id="presets" type="application\/json">).*?(<\/script>)/s;
if (!pattern.test(html)) throw Error('Missing preset data section');
const json = JSON.stringify(presets).replaceAll('<','\\u003c');
fs.writeFileSync(file,html.replace(pattern,(_,begin,end) => begin + json + end));
