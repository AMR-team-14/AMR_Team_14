// Render the trees listed in insights/trees/manifest.json to PNG with the team tree viewer (docs/tree-viewer).
//   node render-trees.cjs            renders only PNGs that do not exist yet (file names carry the answer hash)
// Needs Playwright with Chromium (`require('playwright')`). The viewer page is opened from the repository and its own
// show(csv, label) draws the answer, so the pictures look exactly like the viewer.
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const REPO = path.resolve(__dirname, '..', '..', '..');
const TREES = path.join(REPO, 'insights', 'trees');
const VIEWER = path.join(REPO, 'docs', 'tree-viewer', 'index.html');

(async () => {
  const list = JSON.parse(fs.readFileSync(path.join(TREES, 'manifest.json'), 'utf8')).filter(m => !fs.existsSync(path.join(TREES, m.png)));
  if (!list.length) { console.log('render-trees: nothing new'); return; }
  const {chromium} = require('playwright');
  const browser = await chromium.launch(process.env.CHROME_PATH ? {executablePath: process.env.CHROME_PATH} : {});
  const page = await browser.newPage({viewport: {width: 1400, height: 900}, deviceScaleFactor: 2});
  await page.goto('file://' + VIEWER);
  for (const m of list) {
    const csv = fs.readFileSync(path.join(TREES, m.csv), 'utf8');
    await page.selectOption('#case', String(m.case));
    await page.evaluate(([text, label]) => show(text, label), [csv, m.label]);   // eslint-disable-line no-undef
    const err = await page.textContent('#status');
    if (err && err.trim()) { console.error(`render-trees: ${m.csv}: ${err.trim()}`); continue; }
    await page.locator('#panel').screenshot({path: path.join(TREES, m.png)});
    console.log('render-trees: ' + m.png);
  }
  await browser.close();
})().catch(e => { console.error(e); process.exit(1); });
