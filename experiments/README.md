# 실험 기록

모든 팀원이 같은 구조로 기록한다. 실험 하나는 폴더 하나다.

~~~text
experiments/
  _template/                 새 실험을 만들 때 복사
  common/                    팀 공통 실험 (대수 조사 등)
  <개인 폴더>/               KMJ, PJH, OJT, YB, JGD, JSM
    root-plan.md             내 계열의 가설과 실험 순서
    <ID>-<짧은 이름>/        예: U01-layer2
      README.md              가설 · 조건 · 결과 · 다음 판단
      run.json               대수 · 시드 · 기준 트리 · 상태
      trees/*.csv            비교할 트리 (key 행만)
      results/               실행기가 채움 (손으로 고치지 않음)
        summary.csv          트리 × 대수별 비용, 반려, 대기, 기준 대비 차이
        raw.jsonl.gz         시드별 비용·대기 (gzip)
        meta.json            엔진 커밋, 시드, 실행 위치, 소요 시간
~~~

- **ID:** 계열 문자 + 두 자리 번호. `D` 거리, `W` 장기 대기, `Q` 적체, `U` 긴급, `L` 적재 거리, `F` 대수 조사, `X` 계열 간 비교, `T` 점검.
- **트리 파일:** `key,id,변수,min|max,임계값,lo,hi` 행만 쓴다. `dir`과 `fleet`은 실행기가 붙인다.
- **run.json:** `fleets`는 대수 목록, `seeds`는 `"1001-1064"`처럼 범위 표기 가능, `baseline`은 짝 비교 기준 트리 파일명이다. 트리마다 대수가 다르면 `trees.json`에 `{"파일.csv": [18, 19]}`로 적는다. `status`는 `planned` → `ready`로 바꾸면 실행된다.
- **결과 읽기:** `deltaVsBaseline`이 양수면 기준보다 싸다. `deltaSE`와 비교해 차이가 작으면 시드를 늘린다. `rejected`가 0이 아닌 행은 비용과 무관하게 탈락이다.
- **시드:** 탐색은 모두 `1001-1064`(64개)를 쓴다. 선별은 2001~2200, 최종 검증은 3001~4000으로 따로 둔다.
- **강건성:** 기말 채점이 비공개 시드 1개이므로 `summary.csv`의 반려 수, `costSD`(시드별 비용 표준편차), `costP95`(나쁜 쪽 5% 비용)를 평균과 함께 본다.
- **층별 자동 확장:** `search-<계열>.json`에 계열 상태를 두고 `node tools/grow.mjs experiments/<개인>/search-U.json`을 실행하면, 마지막 층 결과에서 생존 트리를 골라 다음 층 폴더를 만든다. 생존 기준은 반려 없음 + 계열 최선과의 시드별 차이가 2 표준오차 안이다.
- **최종 후보:** 제출할 CSV만 `solutions/<개인 폴더>/`에 올린다.

## 실행

**GitHub (기본):** `run.json`의 `status`를 `ready`로 바꿔 푸시하면 Actions가 결과 없는 실험을 10개 작업으로 나눠 돌리고 `results/`를 같은 브랜치에 커밋한다. 공개 저장소라 무료이며 결과와 실행 기록이 남는다. 받은 뒤 `git pull`.

**로컬 (빠른 확인):**

~~~sh
node tools/build.mjs native
node tools/run-batch.mjs experiments/JSM/U01-layer2          # 결과가 있으면 --force
~~~

**후보 생성:** 부모 트리의 빈 자식 자리 하나에 규칙을 하나씩 붙인 트리를 모두 만든다. [설계 문서](../docs/notes/experiment-design.md) 4.1절·5절의 자동 적용 규칙으로 무효·중복 후보는 만들지 않는다.

~~~sh
node tools/expand.mjs <부모.csv> <노드번호:lo|hi> <실험 폴더>/trees
~~~

## 케이스별 결과 로그 (`experiments/caseN/`)

위 구조는 Case 1 개인 실험 폴더용이다. Case 1~5의 결과를 팀 전체에서 한 표로 비교할 때는 `experiments/caseN/<작성자>-<배치ID>.csv`를 쓴다. 파일 하나는 배치 하나이고, **한 행은 후보 하나를 시드 한 개 또는 공식 집계 단위인 5시드 묶음 하나로 평가한 결과**다. 파일은 추가만 하고 고치지 않는다. 다시 돌리면 새 배치 ID를 쓴다.

~~~text
experiments/
  case2/
    JSM-B1.csv               배치 결과 (아래 열)
    candidates/<해시12>.csv  후보 원문 (dir, fleet, key 행 — 제출 형식 그대로)
    candidates/index.csv     후보 해시 · 이름 · 출처
~~~

| 열 | 내용 |
|---|---|
| case, author, batch | 케이스 번호, 작성자, 배치 ID |
| hypothesis | 이 행이 답하려는 질문 |
| candidate, fleet, dir, tree | 후보 이름, 차량 대수, 통로 방향 비트, key 행(`;` 구분) |
| candidate_path, candidate_sha256 | 후보 원문 경로와 그 파일의 SHA-256 (노드를 BFS 순서로 다시 번호 매긴 LF 텍스트) |
| input_sha256 | 실제 케이스 입력(`inputs.json`의 해당 케이스 `p` 객체)의 SHA-256 |
| evaluator | 평가기 버전 (예: `jsim:e2dc473b4509+ev1` = 엔진 파일 해시 + 집계 코드 판) |
| seeds, aggregation | 시드 목록(`11-15`) · 집계 방식(`pooled-5seed-p95floor` = 웹 방식: 묶음 대기시간을 합쳐 평균과 floor(0.95(N−1)) 긴급 분위) |
| score, vehicle_cost, wait_cost, urgent_cost, mean_wait_min, urgent_p95_min | 총비용과 구성. 반려 묶음은 score를 비운다 |
| rejected, rejected_seeds | 반려 여부(0/1)와 반려된 시드 전부 |
| status | `done` 또는 `failed` (실패도 지우지 않는다) |
| match | `cross-environment`(환경 간 대조용 중복 실행), `교차검증: reported …`(같은 후보·시드를 다른 방식으로 낸 기존 값), `search-objective`(탐색기가 목적함수로 쓴 공개 묶음 값) |
| env, elapsed_ms, raw_log | 실행 위치(local / github / kaggle), 소요 시간, 원시 로그 경로와 실행 ID |
| note | 기타. 확인할 수 없는 옛 기록의 필드는 `미상` |

**재사용 규칙.** `case · candidate_sha256 · input_sha256 · evaluator · seeds · aggregation`이 같은 `done` 행이 이미 있으면 다시 돌리지 않는다. 후보와 시드는 같은데 평가기나 집계 방식이 다르면 새로 돌리고 `match`에 `교차검증`으로 적는다. 같은 키인데 점수가 다르면 충돌이므로 실행 환경과 입력 해시부터 대조한다.

**평가기.** `experiments/JSM/five-case/`의 `lib.cjs`(평가) · `run.cjs`(작업 실행, 완료분 건너뛰고 재시작) · `collect.cjs`(예정 행 수 · 누락 · 중복 · 충돌 검사 후 CSV 작성). Node.js 표준 라이브러리만 쓴다. 후보 하나를 확인할 때:

~~~sh
cd experiments/JSM/five-case
node -e "const L=require('./lib.cjs');const d=L.parseDesign(require('fs').readFileSync('../../case3/candidates/00fad1b682e2.csv','utf8'));console.log(L.evaluate(3,d,11,5).score)"   # 962.7048
~~~

`team-experiments/`는 각 브랜치에 흩어진 기존 기록의 보관·색인이고, 이 폴더는 공통 평가기로 다시 잰 새 배치 결과다.
