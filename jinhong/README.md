# jinhong/ — 진홍의 실험 브랜치 (`jinhong-experiments`)

**지금 상태(11/23 20:30):** Case 1 · 2를 제출했고 둘 다 리더보드 1위다. Case 3~5는 진행 중이다. Case 2~5 계획은 [PLAN_cases.md](PLAN_cases.md), 공통 순서는 [PIPELINE.md](PIPELINE.md)에 있다.

## Case별 결과 한눈에
| 케이스 | 제출 점수 · 순위 | 기준선 대비 | 답 파일 | 기록 | 다시 돌리는 명령 |
|---|---|---|---|---|---|
| Case 1 | **1,170.20** · 1위 (11/23 18:15 제출, 2위 1,178.00) | 팀 기존 최고 1,185.42 → 1,170.20 | [answer_jinhong_leaderboard.csv](answer_jinhong_leaderboard.csv) (21대) | [CASE1_LOGIC.md](CASE1_LOGIC.md) · [EXPERIMENTS.md](EXPERIMENTS.md) | `node fetch_engine.js && node cpp/dump_case.js` 뒤 `node cx_verify.js designs/team_1185.csv answer_jinhong_leaderboard.csv` |
| Case 2 | **617.16** · 1위 (11/23 19:53 제출, 2위 779.98) | 초기 방향 + FCFS 1,251.90 → 617.16 (−634.74, −50.7%) | [case2/answer_case2.csv](case2/answer_case2.csv) (26대, 기준 3개) | [case2/NOTES.md](case2/NOTES.md) | `node pipe.js 2 eval case2/answer_case2.csv` 와 `node pipe.js 2 safety case2/answer_case2.csv 900001 1000` |
| Case 3 | 진행 중 | | | | |
| Case 4 | 진행 중 | | | | |
| Case 5 | 진행 중 | | | | |

- 숫자는 기록 파일에 적힌 것 그대로다. Case 1은 공개 시드 점수(리더보드)이고, 새 시드 평균이 가장 좋은 안은 `answer_jinhong_best.csv`로 따로 둔다(CASE1_LOGIC.md 6절).
- 다시 돌리려면 케이스 데이터가 로컬에 있어야 한다. Case 1은 `fetch_engine.js`가 웹앱에서 받아 오고, Case 2~5는 웹앱 「② 데이터 내려받기」로 받은 JSON을 `C:\dev\ie325-amr\cases\`에 둔다(리포에는 올리지 않는다). 엔진 빌드는 PIPELINE.md 0절.

## GitHub Actions 탐색 (공개 점수를 더 낮추는 계산을 러너에서)
| 케이스 | 워크플로 | 조각 스크립트 | 시작하는 법 | 결과 모으기 |
|---|---|---|---|---|
| Case 1 | `.github/workflows/jinhong-case1.yml` · `jinhong-case1-deep.yml` | `gha_case1.js` · `gha_case1_deep.js` | `gha/trigger_case1.txt`를 고쳐 푸시 (11/23 17:21 시작, 20조각 × 3파도) | `gh run download <실행 번호> -D gha/dl` 뒤 `node gha_collect.js` |
| Case 2 | `.github/workflows/jinhong-case2.yml` | `gha_case2.js` (`pipe.js search`를 씨앗만 바꿔 부른다) | `gha/trigger_case2.txt`를 고쳐 푸시. **실행 45272859577**(11/24 06:45 시작, 20조각 × 300분, 12:00쯤 끝남) | `gh run download 45272859577 -D gha/dl2` 뒤 `node gha_case2_collect.js` |

Case 2 실행 기록(11/24):
- Case 1 두 실행이 06:43에 모두 끝난 뒤 06:45에 시작했다. 20개 조각 모두 러너(리눅스)에서 엔진 빌드를 통과하고 탐색 단계에 들어갔다(06:48 확인). 조각 스크립트는 제출안이 617.16으로 나오지 않으면 탐색 전에 멈추므로, 러너의 엔진과 데이터가 로컬과 같은 값을 낸 것이다. 정확한 값은 끝난 뒤 아티팩트의 `shard_<k>.log` 첫 줄에서 본다.
- 끝난 뒤: 위 두 명령으로 모은다. 617.16보다 낮은 안이 있으면 `case2/gha_top_1.csv`에 대해 `node pipe.js 2 eval`(웹 형식 검사) → `safety` → 웹 「평가 실행」 대조 순서로 확인한다. 제출은 진홍이 정한다.

Case 2를 다시 실행하기 전에 볼 것:
1. **케이스 데이터는 시크릿으로 넣는다(11/23 등록함).** Case 1은 채점 코드와 데이터가 웹 페이지 안에 들어 있어 러너가 직접 받아 오지만, Case 2~5의 데이터는 페이지가 서버 호출로 받아 오기 때문에 그 방법을 쓸 수 없다. 리포에 올리지 않으려면 리포 폴더에서 `cmd /c "gh secret set AGV_CASE2_JSON < C:\dev\ie325-amr\cases\final-agv-c2.json"`으로 넣는다(파일 내용이 글자 깨짐 없이 그대로 들어간다). 워크플로는 이 값을 러너 임시 폴더에만 쓰고 아티팩트에는 넣지 않는다.
2. **러너는 한 번에 20개까지 돈다.** 다른 실행(Case 1 등)이 20개를 쓰고 있으면 그 뒤에 줄을 선다. 같이 돌리려면 조각 수를 줄인다.

Case 2 조각 하나가 하는 일: 엔진을 빌드하고, 제출안이 617.16으로 나오는지 먼저 확인한 뒤(다르면 멈춤), 60분짜리 탐색을 씨앗을 바꿔 5번 돌린다. 시작안은 조각 · 회차마다 세 묶음(제출안만 / + 다른 방향 계열의 구조안 / + 방향 순위 상위 10개)을 번갈아 쓴다. 617.16보다 낮은 안이 나오면 다음 회차의 시작안에 넣는다. 결과는 `search_s<씨앗>_best.csv`와 로그가 아티팩트 `case2-shard-<k>`로 남는다. 모은 뒤에는 반드시 `node pipe.js 2 eval`(웹 형식 검사)과 `safety`를 거친다.

Case 1의 논리 과정은 [CASE1_LOGIC.md](CASE1_LOGIC.md), 실험 기록은 [EXPERIMENTS.md](EXPERIMENTS.md)에 있다.

main의 C++ 엔진과는 따로, **웹 채점 코드를 그대로 써서** 점수를 재고 탐색한 기록을 쌓는 곳이다. 실험 기록은 [EXPERIMENTS.md](EXPERIMENTS.md)에 날짜순으로 덧붙인다.

## 왜 따로 만들었나
- 웹 페이지 안의 `agvRng_` ~ `agvEvaluate_`를 한 글자도 고치지 않고 Node로 불러 쓴다.
  - 그래서 공개 시드 점수가 웹 리더보드와 **소수점까지 같다**: 1185.42 / 1201.39 / 1205.54 / 1207.63 확인.
- main 엔진의 결과를 독립적으로 교차 검증하는 용도다. 시드도 main이 쓰지 않은 5001번대를 쓴다.

## 실행 (Node 18+, 설치할 것 없음)
```bash
cd jinhong
node fetch_engine.js                          # 웹앱에서 채점 코드를 받아 web/engine_raw.js로 저장(리포에는 안 올림)
node eval.js designs/team_1185.csv            # 공개 시드 11~15 = 리더보드 점수
node eval.js designs/team_1185.csv --seeds 101-150 --n 18,19
node final.js designs/robust_best_19.csv designs/team_1185.csv   # 미접촉 시드 5001~5100으로 기준 대비 짝 비교
W=13 NTRAIN=60 node evo.js designs/team_1185.csv 50 runs/x 101   # 나무 탐색(아래 참고)
```

## 파일
| 파일 | 하는 일 |
|---|---|
| `fetch_engine.js` | 웹앱 `?p=case`의 채점 코드를 내려받아 Node 모듈로 저장 |
| `eval.js` | 웹 원본 엔진으로 CSV 평가(`--seeds`, `--n`) |
| `fast.js` | 배차 함수만 다시 짠 빠른 판(나머지는 웹 원본). 웹 원본과 결과가 같은지 `node fast.js <csv>`로 확인 |
| `blocks.js` | 시드를 5개씩 묶어 「비공개 시드가 5개라면」 순위가 얼마나 흔들리는지 |
| `final.js` | 미접촉 시드 5001~5100(100개)과 공개 시드로 기준안 대비 짝 비교(평균 차 ± 표준오차, 묶음 승수) |
| `search.js` | 실험 3: 빈자리 두 곳 교체 |
| `grid.js` | 실험 8: 두 노드 경계값 격자를 같은 시드로 짝 비교 |
| `evo.js` | 실험 4~: (1+26) 진화 탐색. 한 번에 1~4곳 변경, 학습 시드에서 좋아지면 확인 시드에서도 좋아져야 받아들임 |

## 웹 채점 방식에서 확인한 것 (탐색할 때 주의)
- 비용은 시드별 비용의 평균이 **아니다**. 모든 시드의 대기시간을 한데 모아 W̄와 긴급 95분위를 한 번 계산한다(`agvEvaluate_`).
- 배차 키는 `[나무 경로의 비트들..., de, 요청 도착시각, 요청 id, AGV id]`의 사전식 최소다. 나무 다음 우선순위는 가까운 차(STTF), 그다음 FCFS다.

## 11/23에 더한 것: 웹과 같은 C++ 엔진과 단계 선별 탐색
```bash
cd jinhong
node fetch_engine.js && node cpp/dump_case.js                 # 웹 채점 코드와 케이스 표 받기
g++ -O2 -std=c++17 -ffp-contract=off -static -pthread cpp/agvx.cpp -o cpp/agvx.exe
node cx_verify.js                                             # C++ 결과가 웹 원본 JS와 같은지 대조
node final2.js 300001 1000 answer_jinhong_best.csv            # 민재 · 승민 최고안과 짝비교 + 공개 점수
node diag.js answer_jinhong_best.csv                          # 대기가 어느 상황에서 생기는지
node race.js designs/b2_n21.csv 30 runs/x 1 oldonly=1         # 단계 선별 탐색(30분)
node tickets2.js designs/b2_n21.csv 14                        # 기대값이 같은 후보 중 공개 점수 낮은 것 고르기
```

| 파일 | 하는 일 |
|---|---|
| `cpp/agvx.cpp` | 웹 채점 엔진을 연산 순서까지 그대로 옮긴 C++ 판(난수 · 사건 순서 · 배차 키 비교가 웹과 같다) |
| `cx.js` · `cx_verify.js` | C++ 엔진 연결과 웹 원본 대조 |
| `diag.js` | 배차를 「도착 즉시 / 대기 후」로 나눈 대기 분해 |
| `lib_race.js` · `race.js` | 같은 시드 짝비교로 후보를 단계마다 줄이는 탐색 |
| `exp10_ablation.js` ~ `exp13_hybrid.js` | 실험 10~13 재현 스크립트 |
| `tickets2.js` | 공개 점수 고르기(실험 14) |
| `final2.js` | 최종 판정(실험 15) |

시드 구간: 학습 100001~, 확인 200001~, 최종 300001~301000(이미 한 번 썼으므로 다음 판정은 400001~).
