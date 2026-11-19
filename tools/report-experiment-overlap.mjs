import { execFileSync, spawnSync } from "node:child_process";
import { branchCandidates } from "./branch-candidates.mjs";
import { createHash } from "node:crypto";
import { copyFileSync, mkdirSync, mkdtempSync, writeFileSync, readdirSync } from "node:fs";
import { join, relative, sep } from "node:path";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL("..", import.meta.url));
const solutions = join(root, "solutions");
const engine = join(root, "build", process.platform === "win32" ? "agv_opt.exe" : "agv_opt");
const seeds = Array.from({ length: 16 }, (_, index) => 1001 + index);
const owners = { KMJ: "곽민재" };

function candidates(directory) {
  return readdirSync(directory, { withFileTypes: true })
    .sort((a, b) => a.name.localeCompare(b.name))
    .flatMap(entry => {
      const path = join(directory, entry.name);
      if (entry.isDirectory()) return candidates(path);
      return entry.isFile() && /\.csv$/i.test(entry.name) ? [path] : [];
    });
}

function pathsByNode({ nodes }) {
  const paths = new Map();
  function visit(id, path) {
    if (!id) return;
    const node = nodes[id - 1];
    paths.set(path, node);
    visit(node.low, path + "/lo");
    visit(node.high, path + "/hi");
  }
  visit(1, "루트");
  return paths;
}

function groupBy(items, key) {
  const groups = new Map();
  for (const item of items) {
    const value = key(item);
    if (!groups.has(value)) groups.set(value, []);
    groups.get(value).push(item);
  }
  return groups;
}

function code(value) {
  return "<code>" + String(value).replace(/[&<>]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;" })[c]) + "</code>";
}

function cite(file) {
  const label = code(file.path);
  if (file.refs) return file.refs.map(({branch, commit}) => "[" + code(branch) + " " + label + "](https://github.com/" + (process.env.GITHUB_REPOSITORY || "AMR-team-14/AMR_Team_14") + "/blob/" + commit + "/" + file.path.split("/").map(encodeURIComponent).join("/") + ")").join(", ");
  if (!process.env.GITHUB_SHA) return label;
  const blob = file.path.split("/").map(encodeURIComponent).join("/");
  return "[" + label + "](https://github.com/" + process.env.GITHUB_REPOSITORY + "/blob/" + process.env.GITHUB_SHA + "/" + blob + ")";
}

function designLabel(design) {
  return design.files.map(file => file.ownerName + " " + cite(file)).join(", ");
}

function rule(node) {
  return node ? [node.feature, node.mode, node.threshold].join(" ") : "종료";
}

function differences(first, second) {
  const changes = [];
  if (first.policy.fleet !== second.policy.fleet) changes.push(first.policy.fleet + " → " + second.policy.fleet + "대");
  const positions = new Set([...first.paths.keys(), ...second.paths.keys()]);
  for (const position of positions) {
    const before = rule(first.paths.get(position)), after = rule(second.paths.get(position));
    if (before !== after) changes.push(position + " " + before + " → " + after);
  }
  return changes;
}

function localFiles() {
  return readdirSync(solutions, { withFileTypes: true })
  .filter(entry => entry.isDirectory())
  .sort((a, b) => a.name.localeCompare(b.name))
  .flatMap(({ name: owner }) => candidates(join(solutions, owner)).map(fullPath => ({
    path: relative(root, fullPath).split(sep).join("/"),
    owner, ownerName: Object.hasOwn(owners, owner) ? owners[owner] : code(owner)
  })));

}
const remote = process.argv.includes("--all-branches");
const bundle = remote ? mkdtempSync(join(root, "build", "overlap-report-")) : null;
const inputFiles = remote ? branchCandidates(root, join(bundle, "inputs")) : localFiles();
const files = inputFiles.map(file => ({ ...file, ownerName: Object.hasOwn(owners, file.owner) ? owners[file.owner] : code(file.owner) }));

const designs = new Map();
const invalid = [];
for (const file of files) {
  const parsed = spawnSync(engine, ["--policy-json", file.local ?? file.path], { cwd: root, encoding: "utf8" });
  if (parsed.error || parsed.status === null) throw parsed.error ?? new Error("Policy validator stopped: " + parsed.signal);
  if (parsed.status !== 0) {
    const reason = parsed.stderr.trim();
    if (!/^(CSV line \d+:|Policy CSV exceeds|Missing dir\/fleet)/.test(reason)) throw new Error(reason);
    invalid.push([file, reason]);
    continue;
  }
  const policy = JSON.parse(parsed.stdout);
  const key = JSON.stringify(policy);
  if (!designs.has(key)) designs.set(key, { policy, paths: pathsByNode(policy), files: [] });
  designs.get(key).files.push(file);
}

for (const design of designs.values()) {
  const result = execFileSync(engine, ["--eval-json", design.files[0].local ?? design.files[0].path, "--seeds", seeds.join(",")], {
    cwd: root, encoding: "utf8"
  });
  const { scores, average } = JSON.parse(result);
  design.rejected = scores.filter(score => score.rejected).length;
  design.average = average;
  design.scores = scores;
}

const ranked = Array.from(designs.values())
  .sort((a, b) => a.rejected - b.rejected || (a.average ?? Infinity) - (b.average ?? Infinity));
const overlaps = { exact: [], skeleton: [], branches: [] };

function scope(files) {
  return new Set(files.map(file => file.owner)).size > 1 ? "**담당자 간 겹침**" : "한 담당자 안";
}

const rootAndBranches = ["루트", "루트/lo", "루트/hi"];
const skeletonKey = design => JSON.stringify([...design.paths].map(([position, node]) => [position, node.feature]));
const upperKey = design => JSON.stringify(rootAndBranches.map(position => design.paths.get(position)?.feature ?? null));
for (const group of groupBy(ranked, upperKey).values()) {
  for (const [index, first] of group.entries()) {
    if (first.files.length > 1) {
      overlaps.exact.push("- " + scope(first.files) + " · " + designLabel(first));
    }
    for (const second of group.slice(index + 1)) {
      const kind = skeletonKey(first) === skeletonKey(second) ? "skeleton" : "branches";
      const changes = differences(first, second).map(code).join(", ");
      overlaps[kind].push("- " + scope([...first.files, ...second.files]) + " · " +
        designLabel(first) + " ↔ " + designLabel(second) + " — " + changes);
    }
  }
}

const lines = [
  "# 겹치미 · 배차 후보 취합",
  "",
  "공통 시드 1001–1016 · 제출 " + files.length + "개 · 고유 정책 " + ranked.length + "개 · 형식 오류 " + invalid.length + "개",
  "",
  "담당자 간 겹침이 보이면 두 사람이 해당 가지의 의도와 비교할 다음 후보를 맞춥니다.",
  ""
];
for (const [title, entries] of [
  ["동일 정책", overlaps.exact],
  ["같은 골격에서 바뀐 값", overlaps.skeleton],
  ["같은 루트·직계 가지 변수에서 바뀐 규칙", overlaps.branches]
]) {
  lines.push("## " + title, "", ...(entries.length ? entries : ["해당 없음"]), "");
}
lines.push("## 공통 시드 평가", "");
lines.push(...ranked.map(design => "- " + design.policy.fleet + "대 · 루트 " + code(rule(design.paths.get("루트"))) +
  " · 반려 " + design.rejected + "/16 · 평균 비용 " + (design.average?.toFixed(4) ?? "—") +
  " · " + designLabel(design)));
if (!ranked.length) lines.push("평가할 후보 없음");
lines.push("", "## 탐색 범위", "");
const coverage = groupBy(ranked, design => design.paths.get("루트").feature);
for (const [feature, entries] of coverage) {
  const fleets = groupBy(entries, design => design.policy.fleet);
  const counts = [...fleets.keys()].sort((a, b) => a - b).map(fleet => fleet + "대 " + fleets.get(fleet).length + "개");
  lines.push("- " + code(feature) + " · " + counts.join(", "));
}

const projection = new Set(ranked.map(d => d.policy.fleet + "/" + d.paths.get("루트").feature));
lines.push("", "## 제출된 대수 × 루트", "", "150칸 중 " + projection.size + "칸에 제출이 있습니다. 한 칸 안에도 여러 구조와 임계값이 있습니다.");
for (const feature of ["de", "w", "urg", "qo", "dl"]) {
  const empty = Array.from({length: 30}, (_, i) => i + 1).filter(n => !projection.has(n + "/" + feature));
  lines.push("- " + feature + " 미제출 대수 · " + (empty.join(", ") || "없음"));
}
if (remote) {
  const manifest = [];
  for (const design of ranked) {
    const policyID = createHash("sha256").update(JSON.stringify(design.policy)).digest("hex");
    const inMain = design.files.some(f => f.refs.some(ref => ref.branch === "main"));
    if (!inMain) {
      const directory = join(bundle, "main-additions", "solutions", "collected");
      mkdirSync(directory, {recursive: true});
      copyFileSync(design.files[0].local, join(directory, policyID + ".csv"));
    }
    manifest.push({ policyID, inMain, policy: design.policy, seeds, scores: design.scores,
      sources: design.files.map(({ owner, path, refs }) => ({ owner, path, refs })) });
  }
  writeFileSync(join(bundle, "candidates.json"), JSON.stringify({engineCommit: process.env.GITHUB_SHA || null, candidates: manifest}, null, 2));
  lines.push("", "## main에 추가할 후보", "", "현재 main에 없는 고유 정책 " + manifest.filter(d => !d.inMain).length +
    "개. 실행 첨부의 main-additions 폴더와 candidates.json에서 원본과 시드별 결과를 확인하세요.");
}

const byOwner = new Map();
for (const design of ranked) {
  for (const file of design.files) {
    if (!byOwner.has(file.ownerName)) byOwner.set(file.ownerName, new Set());
    byOwner.get(file.ownerName).add(design);
  }
}
const submitters = [...byOwner];
for (const [index, [name, own]] of submitters.entries()) {
  lines.push("- " + name + " · 고유 후보 " + own.size + "개");
  for (const [other, theirs] of submitters.slice(index + 1)) {
    const shared = [...own].filter(design => theirs.has(design)).length;
    const union = own.size + theirs.size - shared;
    lines.push("- " + name + " ↔ " + other + " · 동일 정책 " + shared + "/" + union + " (" +
      (100 * shared / union).toFixed(1) + "%)");
  }
}

if (invalid.length) {
  lines.push("", "## 형식 오류", "");
  for (const [file, reason] of invalid) lines.push("- " + cite(file) + " · " + code(reason));
}
console.log(lines.join("\n"));
