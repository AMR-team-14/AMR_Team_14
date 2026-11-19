# AMR_Team_14 · AGV 배차 결정 트리 최적화

**최종 제출 1순위 ([answer_final_best.csv](./answer_final_best.csv), `n=19`):**
- **라이브 웹 채점:** **`1,185.42 천원/시간`** *(기존 리더보드 1위 `1,198.16` 대비 `-12.74점` 돌파)*
- **미접촉 800시드(`201..1000`):** **`1,191.27 ± 11.18`** ($\overline{W}=3.7887$, $U_{95}=5.1891$, 최대 대기열 `36`, 실격 **`0/800`**)
- **검증 상태:** 브루트포스 정합성 `0.000000000` 일치, 웹 Seed 11 로그 770연속 배차 0오차 일치, **방법 A 전수 검증 통과 — 노드 2개 변경 기준 $\varepsilon$-국소최적 ($\varepsilon = 0.5$점, 95% 신뢰, `FRESH3600 = 1,191.49`, 실격 `0/3,600`)**

---

## 1. 빠른 실행 방법 (C++20 단일 엔진)

입력 데이터([agv_case_data (1).json](./agv_case_data%20(1).json), [agv_dispatch_log_seed11 (1).csv](./agv_dispatch_log_seed11%20(1).csv))가 포함되어 있어 **컴파일 후 즉시 실행 가능**합니다.

```bash
# 1. 컴파일 (C++20, 경고 0건)
g++ -O3 -std=c++20 -s -Wall -Wextra -o agv_opt.exe agv_opt.cpp

# 2. 제약조건 + 브루트포스 15스트림 정합성 검증 (max diff = 0.000000000 확인)
./agv_opt.exe --verify answer_final_best.csv

# 3. 평가 실행 (공개 5시드 / 100시드 / 미접촉 300시드 / 미접촉 800시드)
./agv_opt.exe --eval answer_final_best.csv
./agv_opt.exe --eval100 answer_final_best.csv
./agv_opt.exe --eval300 answer_final_best.csv
./agv_opt.exe --eval800 answer_final_best.csv

# 4. 대수 민감도(n=16..23), 노드 Ablation, 경계값 민감도 전체 분석
./agv_opt.exe --analyze answer_final_best.csv
```

---

## 2. 문서 및 파일 안내

| 파일 | 설명 |
|---|---|
| [answer_final_best.csv](./answer_final_best.csv) | **최종 제출 1순위 (`n=19`, 웹 `1,185.42`, `ALL800 = 1,191.27 ± 11.18`)** |
| [deep_best_18.csv](./deep_best_18.csv) | `n=18` 웹 검증 대안 (`웹 1,189.44`, `ALL800 = 1,191.51 ± 12.58`) |
| [agv_opt.cpp](./agv_opt.cpp) | 독립형 C++20 시뮬레이터·검증기·최적화기 통합 소스코드 |
| [agv_case_data (1).json](./agv_case_data%20(1).json) / [agv_dispatch_log_seed11 (1).csv](./agv_dispatch_log_seed11%20(1).csv) | 과제 입력 JSON 및 웹 시드 11 배차 로그 |
| [README_팀공유_요약.md](./README_팀공유_요약.md) | 팀원용 파이프라인 요약 및 파일 목록 |
| [final_agv_report.md](./final_agv_report.md) | `urg min 0.5` 도메인 돌파구, 300/800시드 검증, 민감도·Ablation 최종 리포트 |
| [algorithm_methodology_report.md](./algorithm_methodology_report.md) | 시뮬레이터 고속화, X-Ray 진단, AST GP·LNS·5-트랙 탐색 알고리즘 방법론 보고서 |
| [local_optimality_proposal.md](./local_optimality_proposal.md) | 2-노드 동시 변경 이웃 전수 국소최적성 검증(방법 A) 제안서 |
| [method_a_local_optimality_report.md](./method_a_local_optimality_report.md) | **방법 A (`4,093`개 전수 이웃 · `FRESH800`/`FRESH3600`) 국소최적성 검증 완료 보고서** |
| [agent.md](./agent.md) | AI 에이전트용 제약조건·물리 엔진 명세·과적합 방지 프로토콜 |

---

## 3. 앞으로 할 일 (Next Steps)

1. **최종 제출 확정:** 현재 [answer_final_best.csv](./answer_final_best.csv) (`1,185.42`)를 최종 제출본으로 유지 (비공개 시드 일반화 분산 `±11.18` 최저, `FRESH3600 = 1,191.49`, 실격 `0/3,600`).
2. **최종 보고서/발표 작성:** [final_agv_report.md](./final_agv_report.md), [algorithm_methodology_report.md](./algorithm_methodology_report.md), [method_a_local_optimality_report.md](./method_a_local_optimality_report.md) 기반으로 논리 정리 (2-노드 이웃 기준 $\varepsilon=0.5$, 95% 신뢰 국소최적 정당화 문장 수록 완료).
