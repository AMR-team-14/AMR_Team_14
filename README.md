# 물류관리 실험 · jsm

다섯 케이스 실험을 GitHub Actions에서 자동으로 돌리고(실험 → 팀 답안 수집·재채점 → AI 분석 → 다음 실험), 결과를 아래에 모은다.

- **최고 기록**: 아래 표(기록이 바뀔 때 자동 갱신), 답안 파일은 [`best/`](best/)
- **상위권 답안·트리 그림·전략**: [`insights/TOP.md`](insights/TOP.md)
- **인사이트**: [`insights/known-insights.md`](insights/known-insights.md)(자동), [`insights/INSIGHTS.md`](insights/INSIGHTS.md)(팀 전체 총정리)
- **시스템 설명**: [`experiments/common/five-case/README.md`](experiments/common/five-case/README.md)
- 공통 요구사항: [simulator/spec/REQUIREMENTS.md](simulator/spec/REQUIREMENTS.md) · 평가: `node simulator/score.cjs <케이스> <답안> 1 500 --trace`

<!-- BOARD:START (자동 갱신: experiments/common/five-case/readme-board.cjs) -->
## 최고 기록 (팀 전체 답안, 공통 평가기)

모든 브랜치의 답안을 `simulator/score.cjs`로 다시 채점한 값이다. 비용이라 낮을수록 좋다. 기록이 바뀔 때만 이 표와 `best/` 파일이 갱신된다.
- **대시보드**: 공개 시드 11–15 점수 = 리더보드 성적(Case 2–5). Case 1은 비공개 시드로 채점하므로 참고만.
- **범용성(강건성)**: 시드 1–500 묶음 점수, 반려 0인 답만. Case 1은 이 값이 기준.

| 케이스 | 리더보드 1위 (기록 보유자) | 저장소 최고 11–15 | 그 답의 1–500 | 대수 | 저장소 답 만든 사람 | 저장소 답 출처 | 답안 |
|---|---|---|---|---|---|---|---|
| 1 | **1170.20** (박진홍) | **1170.2008** | 1184.0466 | 21대 | 박진홍 | `pjh` PJH-1123-117020/answer.csv | [case1-dashboard.csv](best/case1-dashboard.csv) |
| 2 | **613.61** (윤빈) — 저장소 답이 아직 못 넘음 | **613.6186** | 616.0472 | 24대 | 박진홍, 장승민 | `jinhong-experiments` answer_case2.csv | [case2-dashboard.csv](best/case2-dashboard.csv) |
| 3 | **962.28** (14팀 팀원(누구인지 미확인)) — 저장소 답이 아직 못 넘음 | **962.4482** | 979.5062 | 22대 | 장승민 | jsm 자동 탐색 `auto-11280039` | [case3-dashboard.csv](best/case3-dashboard.csv) |
| 4 | **788.95** (박진홍) — 저장소 답이 아직 못 넘음 | **789.2041** | 793.3858 | 18대 | 장승민 | jsm 자동 탐색 `auto-11272053` | [case4-dashboard.csv](best/case4-dashboard.csv) |
| 5 | **770.82** (박진홍) — 저장소 답이 아직 못 넘음 | **770.8816** | 786.7907 | 23대 | 박진홍, 장승민 | `jinhong-experiments` cand_77088.csv | [case5-dashboard.csv](best/case5-dashboard.csv) |

리더보드 1위 칸만 리더보드 기록이고, 나머지 칸은 저장소에 푸시된 최고 답안에 대한 것이다. 리더보드 기록은 답안 파일이 저장소에 없는 기록까지 포함한다(손으로 적음: `experiments/common/five-case/team/leaderboard.json`). 저장소 최고는 푸시된 답안만 본다.

| 케이스 | 범용성 1–500 | 그 답의 11–15 | 대수 | 저장소 답 만든 사람 | 저장소 답 출처 | 답안 |
|---|---|---|---|---|---|---|
| 1 | **1180.5642** | 1185.9410 | 21대 | 장승민 | `jsm` 8e7431131490.csv | [case1-robust.csv](best/case1-robust.csv) |
| 2 | **603.4758** | 663.0128 | 24대 | 장승민 | jsm 자동 탐색 `auto-11271811` | [case2-robust.csv](best/case2-robust.csv) |
| 3 | **974.5400** | 974.4337 | 22대 | 곽민재 | `kmj` KMJ-1127-V3-C3/answer.csv | [case3-robust.csv](best/case3-robust.csv) |
| 4 | **790.4171** | 793.9583 | 18대 | 장승민 | jsm 자동 탐색 `auto-11272134` | [case4-robust.csv](best/case4-robust.csv) |
| 5 | **774.7800** | 777.9406 | 22대 | 장승민 | jsm 자동 탐색 `auto-11271811` | [case5-robust.csv](best/case5-robust.csv) |

전체 순위와 회차별 분석은 [reports/](experiments/common/five-case/reports/), 인사이트는 [insights/](insights/)에 있다.
<!-- BOARD:END -->

## 다시 쓸 도구

- [배차 트리 뷰어](https://amr-team-14.github.io/AMR_Team_14/)에서 Case 1의 트리를 보고 로컬 평가값을 확인한다. [뷰어 원본](docs/index.html)은 GitHub Pages에서 배포한다.
- [뷰어 입력 만들기](tools/prepare-viewer-data.mjs)는 Case 1 입력과 공식 배차 로그로 뷰어 데이터를 갱신한다. 저장소 루트에서 `node tools/prepare-viewer-data.mjs <케이스 JSON> <시드 11 로그 CSV> docs`를 사용한다.
- [main 반영 스킬](.agents/skills/amr-main-handoff/SKILL.md)은 팀이 공유하기로 한 결과·답안·공통 평가기만 골라 반영할 때 사용한다.
