import { execFileSync } from "node:child_process";
import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";

// Read data blobs only. All validation and evaluation use the checked-out engine.
export function branchCandidates(root, inbox) {
  const git = (...args) => execFileSync("git", args, { cwd: root, encoding: "utf8", maxBuffer: 64 * 1024 * 1024 });
  const refs = git("for-each-ref", "--format=%(refname:strip=3)%09%(objectname)", "refs/remotes/origin")
    .trim().split("\n").filter(Boolean).map(row => row.split("\t"));
  if (!refs.some(([name]) => name === "main")) throw new Error("Fetch all origin branches including main first.");
  mkdirSync(inbox, { recursive: true });
  const files = new Map(), blobs = new Map();
  for (const [branch, commit] of refs) {
    if (branch === "HEAD") continue;
    for (const row of git("ls-tree", "-rlz", commit).split("\0")) {
      const match = /^(100(?:644|755)) blob ([0-9a-f]+) +([0-9]+)\t([\s\S]*\.csv)$/i.exec(row);
      if (!match) continue;
      const [, , oid, size, path] = match;
      const personal = /^solutions\/([^/]+)\//.exec(path);
      if (!personal && Number(size) > 8192) continue;
      if (!blobs.has(oid)) {
        const local = join(inbox, oid + ".csv");
        const body = Number(size) > 8192 ? " ".repeat(8193) : git("cat-file", "blob", oid);
        writeFileSync(local, body);
        blobs.set(oid, { local, policyLike: /^(?:\uFEFF)?\s*(?:dir|fleet|key)\s*,/m.test(body) });
      }
      const blob = blobs.get(oid);
      if (!personal && !blob.policyLike) continue;
      const key = oid + ":" + path;
      if (!files.has(key)) files.set(key, { path, local: blob.local, oid, owner: personal?.[1], refs: [] });
      files.get(key).refs.push({ branch, commit });
    }
  }
  const known = new Map();
  for (const f of files.values()) {
    if (f.owner) {
      if (!known.has(f.oid)) known.set(f.oid, new Set());
      known.get(f.oid).add(f.owner);
    }
  }
  for (const f of files.values()) {
    if (!f.owner) f.owner = known.get(f.oid)?.size === 1 ? [...known.get(f.oid)][0] : "작성자 미확인";
  }
  return [...files.values()];
}
