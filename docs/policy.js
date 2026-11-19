export const features = {
  de: ["빈 차 이동거리", " m"], dl: ["적재 이동거리", " m"],
  qo: ["출발지 대기 건수", "건"], urg: ["긴급 여부", ""], w: ["대기 시간", "분"]
};

export function parseCsv(csv, model) {
  const nodes = new Map();
  let fleet;
  let direction;
  for (const [index, raw] of csv.replace(/^\uFEFF/, "").split(/\r?\n/).entries()) {
    if (!raw.trim()) continue;
    const [kind, ...fields] = raw.split(",").map(field => field.trim());
    const invalid = () => { throw new Error(`${index + 1}행 형식을 확인하세요.`); };
    if (kind === "dir") {
      if (fields.length !== 1 || !/^[01]+$/.test(fields[0]) || direction !== undefined) invalid();
      direction = fields[0];
    } else if (kind === "fleet") {
      if (fields.length !== 1 || !/^[1-9]\d*$/.test(fields[0]) || fleet !== undefined) invalid();
      fleet = Number(fields[0]);
      if (!Number.isSafeInteger(fleet)) invalid();
    } else if (kind === "key") {
      if (fields.length !== 6) invalid();
      const [id, feature, mode, threshold, low, high] = fields;
      if (!/^[1-9]$/.test(id) || !Object.hasOwn(features, feature) ||
          !["min", "max"].includes(mode) || !threshold || !Number.isFinite(Number(threshold)) ||
          !/^[0-9]$/.test(low) || !/^[0-9]$/.test(high) || nodes.has(Number(id))) invalid();
      if (feature === "urg" && Number(threshold) !== 0.5) invalid();
      nodes.set(Number(id), {id:Number(id), feature, mode, threshold:Number(threshold), low:Number(low), high:Number(high)});
    } else invalid();
  }
  if (!fleet || !nodes.has(1)) throw new Error("fleet과 1번 노드가 필요합니다.");
  if (fleet < model.nMin || fleet > model.nMax) throw new Error("차량 대수를 확인하세요.");
  if (direction !== model.fixedDir) throw new Error("공식 케이스의 고정 통로 방향과 다릅니다.");
  const visited = new Set();
  function depth(id, used) {
    if (id === 0) return 0;
    const node = nodes.get(id);
    if (!node || visited.has(id)) throw new Error("자식 번호가 없거나 노드가 여러 번 연결돼 있습니다.");
    if (used.has(node.feature)) throw new Error("한 경로에서 같은 변수를 다시 사용할 수 없습니다.");
    visited.add(id);
    const next = new Set([...used, node.feature]);
    return 1 + Math.max(depth(node.low, next), depth(node.high, next));
  }
  const maxDepth = depth(1, new Set());
  if (visited.size !== nodes.size) throw new Error("1번에서 도달할 수 없는 노드가 있습니다.");
  return {fleet, nodes, maxDepth, direction};
}
