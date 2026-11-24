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
export const MAX_NODES = 9;
export const MAX_DEPTH = 5;

export function parseTree(text, label = "tree") {
  const nodes = new Map();
  for (const line of text.split(/\r?\n/)) {
    const parts = line.split(",").map(s => s.trim());
    if (parts[0] !== "key") continue;
    const [, id, feature, mode, threshold, low, high] = parts;
    nodes.set(+id, { id: +id, feature, mode, threshold: +threshold, low: +low, high: +high });
  }
  if (!nodes.has(1)) throw new Error(label + ": tree has no root node 1");
  return nodes;
}

export const readTree = path => parseTree(readFileSync(path, "utf8"), path);

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

// Ancestors of a slot: [{feature, threshold, side}] from the root down, ending with the parent.
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

// Empty child slots where one more node keeps depth <= 5.
export function emptySlots(nodes) {
  if (nodes.size >= MAX_NODES) return [];
  const slots = [];
  for (const n of nodes.values())
    for (const side of ["lo", "hi"])
      if (!n[side === "lo" ? "low" : "high"] && pathTo(nodes, n.id, side).length < MAX_DEPTH) slots.push([n.id, side]);
  return slots;
}

// Every single-node expansion at one slot after the automatic pruning rules
// (docs/notes/experiment-design.md §4.1 and §5).
export function expansionsAt(parent, parentId, side, vars = Object.keys(GRID), grid = GRID) {
  const key = side === "lo" ? "low" : "high";
  const path = pathTo(parent, parentId, side);
  const used = new Set(path.map(p => p.feature));
  const urgent = path.some(p => p.feature === "urg" && p.side === "hi");
  const noUrgent = path.some(p => p.feature === "urg" && p.side === "lo")
    || path.some(p => p.feature === "dl" && p.side === "hi" && p.threshold >= 25);
  const skipped = [];
  const prune = (feature, mode) => {
    if (feature === "de" && mode === "min") return "leaf de(min) equals the default nearest rule";
    if (urgent && feature === "dl") return "A1: urgent loads are all 25 m";
    if (noUrgent && feature === "urg") return "A1: no urgent request in this branch";
    if (urgent && feature === "w" && mode === "max") return "A4: oldest urgent request is already the only candidate";
    if (urgent && feature === "qo") return "A2: urgent candidates share qo at each decision";
    return null;
  };
  const out = [];
  for (const feature of vars) {
    if (used.has(feature)) continue;
    for (const mode of ["min", "max"]) {
      const reason = prune(feature, mode);
      if (reason) { skipped.push(`${feature}(${mode}): ${reason}`); continue; }
      for (const threshold of grid[feature]) {
        const nodes = new Map([...parent].map(([id, n]) => [id, { ...n }]));
        const id = nodes.size + 1;
        nodes.get(parentId)[key] = id;
        nodes.set(id, { id, feature, mode, threshold, low: 0, high: 0 });
        out.push({ nodes, name: `${parentId}${side}_${feature}_${mode}_${threshold}` });
      }
    }
  }
  return { out, skipped };
}

export function seedList(spec) {
  return String(spec).split(",").flatMap(part => {
    const [a, b] = part.split("-").map(Number);
    return b ? Array.from({ length: b - a + 1 }, (_, i) => a + i) : [a];
  });
}
