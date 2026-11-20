# jinhong/ — 진홍의 실험 브랜치 (`jinhong-experiments`)

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
