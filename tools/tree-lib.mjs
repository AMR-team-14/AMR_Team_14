// Shared helpers for experiment tree files. A tree file holds only `key,...` rows;
// the runner adds the fixed `dir` row and the fleet size.
import { readFileSync } from "node:fs";

export const GRID = {
  de: [0, 25, 50, 65, 90, 115, 140, 155, 180],
  dl: [25, 50, 65, 90],
  urg: [0.5],
  w: [0.5, 1, 2, 4, 8, 16, 32],
  qo: [1, 2, 4, 8]
};

export function readTree(path) {
  const nodes = new Map();
  for (const line of readFileSync(path, "utf8").split(/\r?\n/)) {
    const parts = line.split(",").map(s => s.trim());
    if (parts[0] !== "key") continue;
    const [, id, feature, mode, threshold, low, high] = parts;
    nodes.set(+id, { id: +id, feature, mode, threshold: +threshold, low: +low, high: +high });
  }
  if (!nodes.has(1)) throw new Error(path + ": tree has no root node 1");
  return nodes;
}

export function treeText(nodes) {
  return [...nodes.values()].sort((a, b) => a.id - b.id)
    .map(n => ["key", n.id, n.feature, n.mode, n.threshold, n.low, n.high].join(",")).join("\n") + "\n";
}

// Compact notation used in summaries: urg(max,0.5)[w(max,4),-]
export function notation(nodes, id = 1) {
  if (!id) return "-";
  const n = nodes.get(id);
  const self = `${n.feature}(${n.mode},${n.threshold})`;
  return n.low || n.high ? `${self}[${notation(nodes, n.low)},${notation(nodes, n.high)}]` : self;
}

// Ancestors of a slot: [{feature, threshold, side}] from the root down.
export function pathTo(nodes, parentId, side) {
  const parents = new Map();
  for (const n of nodes.values()) {
    if (n.low) parents.set(n.low, { id: n.id, side: "lo" });
    if (n.high) parents.set(n.high, { id: n.id, side: "hi" });
  }
  const path = [{ ...nodes.get(parentId), side }];
  for (let id = parentId; parents.has(id);) {
    const p = parents.get(id);
    path.unshift({ ...nodes.get(p.id), side: p.side });
    id = p.id;
  }
  return path;
}

export function seedList(spec) {
  return String(spec).split(",").flatMap(part => {
    const [a, b] = part.split("-").map(Number);
    return b ? Array.from({ length: b - a + 1 }, (_, i) => a + i) : [a];
  });
}
