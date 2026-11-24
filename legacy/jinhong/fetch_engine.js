// 케이스 웹앱에서 채점 코드를 내려받아 web/engine_raw.js로 저장한다(리포에는 올리지 않음, .gitignore).
// 페이지 안의 agvRng_ ~ DATA_AGV를 손대지 않고 그대로 잘라 Node 모듈로 만든다. 그래서 점수가 웹과 소수점까지 같다.
// 사용: node fetch_engine.js
const https = require('https'), fs = require('fs'), path = require('path');
const URL = 'https://script.google.com/macros/s/AKfycbyfytW6GhEMwkUIE7xja48WGlirBBES_VTXoXxVuzDSHAfcML17mWYwT7VDdBA8gFD9/exec?p=case';

function get(url, n = 0) {
  return new Promise((res, rej) => https.get(url, r => {
    if (r.statusCode >= 300 && r.statusCode < 400 && r.headers.location && n < 5) return res(get(r.headers.location, n + 1));
    let s = ''; r.setEncoding('utf8'); r.on('data', c => s += c); r.on('end', () => res(s));
  }).on('error', rej));
}

(async () => {
  const t = await get(URL);
  const i = t.indexOf('goog.script.init(');
  const m = t.slice(i + 17).match(/^"((?:[^"\\]|\\.)*)"/);
  const html = JSON.parse((0, eval)('"' + m[1] + '"')).userHtml;
  const a = html.indexOf('var AGV_FEATURES'), b = html.indexOf('const D = DATA_AGV');
  if (a < 0 || b < 0) throw new Error('채점 코드 위치를 못 찾음: 웹앱이 바뀌었는지 확인');
  const out = html.slice(a, b) + '\nmodule.exports={agvParse_,agvEvaluate_,agvRun_,agvNet_,DATA_AGV};\n';
  fs.mkdirSync(path.join(__dirname, 'web'), { recursive: true });
  fs.writeFileSync(path.join(__dirname, 'web', 'engine_raw.js'), out);
  console.log('web/engine_raw.js 저장', out.length, '자');
})();
