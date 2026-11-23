# jinhong/ — 진홍의 실험 브랜치 (`jinhong-experiments`)

**지금 상태(11/23 18:20):** Case 1 리더보드 1위 1,170.20(`answer_jinhong_leaderboard.csv`, 21대). 논리 과정은 [CASE1_LOGIC.md](CASE1_LOGIC.md), 실험 기록은 [EXPERIMENTS.md](EXPERIMENTS.md), Case 2~5 계획은 [PLAN_cases.md](PLAN_cases.md)에 있다.

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
