import { spawnSync } from "node:child_process";
import { mkdirSync } from "node:fs";
import { join } from "node:path";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL("..", import.meta.url));
const target = process.argv[2];
const windows = process.platform === "win32";
const core = ["case_data", "policy", "simulator", "evaluation"].map(name => "src/" + name + ".cpp");
function compile(compiler, args) {
  const call = spawnSync(compiler, args, {
    cwd: root, stdio: "inherit"
  });
  if (call.error) throw call.error;
  if (call.status !== 0) process.exit(call.status ?? 1);
}
if (target === "native") {
  mkdirSync(new URL("../build/", import.meta.url), { recursive: true });
  compile(process.env.CXX ?? "g++", [
    "-std=c++20", "-O3", "-pthread", ...(windows ? ["-static"] : []),
    ...core, "src/search.cpp", "src/main.cpp", "-o", "build/agv_opt" + (windows ? ".exe" : "")
  ]);
} else if (target === "web") {
  mkdirSync(new URL("../docs/engine/", import.meta.url), { recursive: true });
  const args = [
    "-std=c++20", "-O3", "-fexceptions", "--no-entry", ...core, "src/web_api.cpp",
    "-sMODULARIZE=1", "-sEXPORT_ES6=1", "-sALLOW_MEMORY_GROWTH=1", "-sSTACK_SIZE=524288",
    '-sEXPORTED_FUNCTIONS=_agv_initialize,_agv_evaluate',
    '-sEXPORTED_RUNTIME_METHODS=cwrap', "-o", "docs/engine/agv.mjs"
  ];
  if (windows && !process.env.EMXX) {
    if (!process.env.EMSDK || !process.env.EMSDK_PYTHON)
      throw new Error("Activate the Emscripten SDK with emsdk_env.bat first.");
    compile(process.env.EMSDK_PYTHON, [join(process.env.EMSDK,"upstream","emscripten","em++.py"), ...args]);
  } else {
    compile(process.env.EMXX ?? "em++", args);
  }
} else {
  console.error("Usage: node tools/build.mjs native|web");
  process.exitCode = 1;
}
