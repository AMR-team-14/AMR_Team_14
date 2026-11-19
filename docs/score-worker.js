import createEngine from "./engine/agv.mjs";

async function prepare() {
  async function read(path) {
    const response = await fetch(path);
    if (!response.ok) throw new Error("평가 데이터를 불러오지 못했습니다.");
    return response.text();
  }
  const [engine, caseJson, seed11] = await Promise.all([
    createEngine(), read("./case.json"), read("./seed11.json")
  ]);
  const initialize = engine.cwrap("agv_initialize", "string", ["string", "string"]);
  const result = JSON.parse(initialize(caseJson, seed11));
  if (result.error) throw new Error(result.error);
  return engine.cwrap("agv_evaluate", "string", ["string"]);
}

let evaluator;
self.addEventListener("message", async ({data}) => {
  try {
    evaluator ??= prepare();
    self.postMessage(JSON.parse((await evaluator)(data)));
  } catch (error) {
    evaluator = undefined;
    self.postMessage({error: error.message});
  }
});
