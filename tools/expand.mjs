// Generate every single-node expansion of a parent tree at one empty slot,
// applying the automatic (logic) pruning rules of docs/notes/experiment-design.md §4.1, §5.
//   node tools/expand.mjs <parent.csv> <nodeId:lo|hi> <out-dir> [--vars w,qo]
import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { GRID, readTree, treeText, notation, pathTo, MAX_NODES, MAX_DEPTH, expansionsAt } from "./tree-lib.mjs";

const [parentPath, slot, outDir, ...rest] = process.argv.slice(2);
if (!parentPath || !/^\d+:(lo|hi)$/.test(slot ?? "") || !outDir) {
  console.error("Usage: node tools/expand.mjs <parent.csv> <nodeId:lo|hi> <out-dir> [--vars de,w]");
  process.exit(1);
}
const vars = rest[0] === "--vars" ? rest[1].split(",") : Object.keys(GRID);
const parent = readTree(parentPath);
const [parentId, side] = [+slot.split(":")[0], slot.split(":")[1]];
if (!parent.has(parentId) || parent.get(parentId)[side === "lo" ? "low" : "high"]) throw new Error("Slot is not an empty child: " + slot);
if (parent.size + 1 > MAX_NODES) throw new Error("Tree already has 9 nodes");
if (pathTo(parent, parentId, side).length + 1 > MAX_DEPTH) throw new Error("Expansion would exceed depth 5");

const { out, skipped } = expansionsAt(parent, parentId, side, vars);
mkdirSync(outDir, { recursive: true });
const index = ["file,tree"];
for (const { nodes, name } of out) {
  writeFileSync(join(outDir, name + ".csv"), treeText(nodes));
  index.push(`${name}.csv,"${notation(nodes)}"`);
}
writeFileSync(join(outDir, `index_${slot.replace(":", "")}.csv`), index.join("\n") + "\n");
console.log(`${out.length} trees at ${slot}; pruned: ${skipped.length ? skipped.join("; ") : "none"}`);
