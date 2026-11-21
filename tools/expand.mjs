// Generate every single-node expansion of a parent tree at one empty slot,
// applying the automatic (logic) pruning rules of docs/notes/experiment-design.md §4.1, §5.
//   node tools/expand.mjs <parent.csv> <nodeId:lo|hi> <out-dir> [--vars w,qo]
import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { GRID, readTree, treeText, notation, pathTo } from "./tree-lib.mjs";

const [parentPath, slot, outDir, ...rest] = process.argv.slice(2);
if (!parentPath || !/^\d+:(lo|hi)$/.test(slot ?? "") || !outDir) {
  console.error("Usage: node tools/expand.mjs <parent.csv> <nodeId:lo|hi> <out-dir> [--vars de,w]");
  process.exit(1);
}
const vars = rest[0] === "--vars" ? rest[1].split(",") : Object.keys(GRID);
const parent = readTree(parentPath);
const [parentId, side] = [+slot.split(":")[0], slot.split(":")[1]];
const key = side === "lo" ? "low" : "high";
if (!parent.has(parentId) || parent.get(parentId)[key]) throw new Error("Slot is not an empty child: " + slot);
if (parent.size + 1 > 9) throw new Error("Tree already has 9 nodes");

const path = pathTo(parent, parentId, side);
if (path.length + 1 > 5) throw new Error("Expansion would exceed depth 5");
const used = new Set(path.map(p => p.feature));
// Urgent branch: an urg ancestor sent this slot to the side holding urg = 1.
const urgent = path.some(p => p.feature === "urg" && (p.side === "hi") === (p.threshold < 1));
// Long-load branch: dl > 25 means no urgent requests (A1).
const noUrgent = path.some(p => p.feature === "urg" && (p.side === "lo") === (p.threshold < 1))
  || path.some(p => p.feature === "dl" && p.side === "hi" && p.threshold >= 25);

const skipped = [];
function prune(feature, mode) {
  if (feature === "de" && mode === "min") return "leaf de(min) equals the default nearest rule";
  if (urgent && feature === "dl") return "A1: urgent loads are all 25 m";
  if (noUrgent && feature === "urg") return "A1: no urgent request in this branch";
  if (urgent && feature === "w" && mode === "max") return "A4: oldest urgent request is already the only candidate";
  if (urgent && feature === "qo") return "A2: urgent candidates share qo at each decision";
  return null;
}

mkdirSync(outDir, { recursive: true });
const index = ["file,tree"];
for (const feature of vars) {
  if (used.has(feature)) continue;
  for (const mode of ["min", "max"]) {
    const reason = prune(feature, mode);
    if (reason) { skipped.push(`${feature}(${mode}): ${reason}`); continue; }
    for (const threshold of GRID[feature]) {
      const nodes = new Map([...parent].map(([id, n]) => [id, { ...n }]));
      const id = nodes.size + 1;
      nodes.get(parentId)[key] = id;
      nodes.set(id, { id, feature, mode, threshold, low: 0, high: 0 });
      const file = `${slot.replace(":", "")}_${feature}_${mode}_${threshold}.csv`;
      writeFileSync(join(outDir, file), treeText(nodes));
      index.push(`${file},"${notation(nodes)}"`);
    }
  }
}
writeFileSync(join(outDir, `index_${slot.replace(":", "")}.csv`), index.join("\n") + "\n");
console.log(`${index.length - 1} trees at ${slot}; pruned: ${skipped.length ? skipped.join("; ") : "none"}`);
