// Read/write experiments/caseN/<author>-<batch>.csv logs and the candidate registry.
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const L = require('./lib.cjs');

const REPO = path.resolve(L.HERE, '..', '..', '..');
const COLS = ['case', 'author', 'batch', 'hypothesis', 'candidate', 'fleet', 'dir', 'tree', 'candidate_path', 'candidate_sha256',
  'input_sha256', 'evaluator', 'seeds', 'aggregation', 'score', 'vehicle_cost', 'wait_cost', 'urgent_cost', 'mean_wait_min',
  'urgent_p95_min', 'rejected', 'rejected_seeds', 'status', 'match', 'env', 'elapsed_ms', 'raw_log', 'note'];

const q = v => {
  const s = v === undefined || v === null ? '' : String(v);
  return /[",\n]/.test(s) ? '"' + s.replace(/"/g, '""') + '"' : s;
};
function parseCsv(text) {
  const rows = [];
  let row = [], cell = '', inQ = false;
  for (let i = 0; i < text.length; i++) {
    const ch = text[i];
    if (inQ) {
      if (ch === '"' && text[i + 1] === '"') { cell += '"'; i++; } else if (ch === '"') inQ = false; else cell += ch;
    } else if (ch === '"') inQ = true;
    else if (ch === ',') { row.push(cell); cell = ''; }
    else if (ch === '\n') { row.push(cell.replace(/\r$/, '')); rows.push(row); row = []; cell = ''; }
    else cell += ch;
  }
  if (cell || row.length) { row.push(cell); rows.push(row); }
  const head = rows.shift() || [];
  return rows.filter(r => r.length > 1).map(r => Object.fromEntries(head.map((h, i) => [h, r[i] ?? ''])));
}
const toCsv = rows => [COLS.join(','), ...rows.map(r => COLS.map(c => q(r[c])).join(','))].join('\n') + '\n';

const caseDir = c => path.join(REPO, 'experiments', 'case' + c);
function allResults(c) {
  const dir = caseDir(c);
  if (!fs.existsSync(dir)) return [];
  return fs.readdirSync(dir).filter(f => f.endsWith('.csv')).flatMap(f =>
    parseCsv(fs.readFileSync(path.join(dir, f), 'utf8')).map(r => ({...r, _file: f})));
}
const seedText = (start, count) => count === 1 ? String(start) : `${start}-${start + count - 1}`;
const keyOf = r => [r.case, r.candidate_sha256, r.input_sha256, r.evaluator, r.seeds, r.aggregation].join('|');

// Candidate files live at experiments/caseN/candidates/<sha12>.csv; index.csv records label and origin.
function saveCandidate(c, cand, label, origin) {
  const dir = path.join(caseDir(c), 'candidates');
  fs.mkdirSync(dir, {recursive: true});
  const rel = `experiments/case${c}/candidates/${cand.sha.slice(0, 12)}.csv`;
  const file = path.join(REPO, rel);
  if (!fs.existsSync(file)) fs.writeFileSync(file, cand.text);
  if (label === null) return rel;
  const idx = path.join(dir, 'index.csv');
  const head = 'candidate_sha256,label,fleet,dir,origin\n';
  const old = fs.existsSync(idx) ? fs.readFileSync(idx, 'utf8') : head;
  const line = [cand.sha, label, cand.design.fleet, cand.design.dir, origin].map(q).join(',');
  if (!old.split('\n').some(l => l.startsWith(cand.sha + ',' ) && l.includes(',' + q(label) + ','))) fs.writeFileSync(idx, old + line + '\n');
  return rel;
}

module.exports = {REPO, COLS, parseCsv, toCsv, allResults, seedText, keyOf, saveCandidate, caseDir};
