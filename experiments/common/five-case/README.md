# 다섯 케이스 자동 실험 (jsm)

실험 → 수집·채점 → 분석 → 다음 실험이 GitHub Actions에서 스스로 이어지는 시스템이다. 숫자는 전부 코드가 계산하고, AI(Claude)는 해석과 다음 실험 선택만 한다.

## 결과를 보는 곳

| 보고 싶은 것 | 위치 |
|---|---|
| 케이스별 최고 기록 (대시보드 11–15 / 범용성 1–500), 리더보드와 비교 | 저장소 [README](../../../README.md) 맨 위 표 |
| 최고 답안 파일 (바로 제출) | [`best/`](../../../best/) `case<N>-dashboard.csv`, `case<N>-robust.csv` |
| 상위권 답안 3개씩 + 트리 그림 + 전략 설명 | [`insights/TOP.md`](../../../insights/TOP.md) |
| 누적 인사이트 (범용 + 케이스별, AI가 매 회차 갱신) | [`insights/known-insights.md`](../../../insights/known-insights.md) |
| 팀 전체 인사이트 총정리 (요청할 때만 갱신) | [`insights/INSIGHTS.md`](../../../insights/INSIGHTS.md) |
| 회차별 보고서 (코드 기본 분석 + AI 심화 분석) | [`reports/`](reports/) `<MMDD-HHMM>-run<id>.md` |
| 팀 전체 답안 점수 원본 | [`team/scores.json`](team/scores.json) |
| AI가 고른 실험과 이유 | [`jobs/`](jobs/) `auto-*.plan.json` |
| AI 사용량 | [`reports/usage.jsonl`](reports/usage.jsonl), 보고서 마지막 줄 |

## 한 회차의 흐름

```
① 실험 실행      jsm-five-case.yml   대기(start-after.txt) → 러너 8대 × 4코어로 jobs/<batch>-g*.jsonl 실행
② 보관           jsm-analyze.yml     Actions 결과를 raw/github/<run_id>/에 보관
③ 팀 답안 수집    team-gather.cjs     모든 브랜치 답안 + jsm 탐색 결과 → 11-15, 1-500으로 재채점 → team/scores.json
④ 통계·기본 보고  digest.cjs          두 순위, 같은 시드 대응 비교, 요인 효과 → digest.json, basic.md
⑤ 심화 분석 (AI) claude -p           digest.json·known-insights.md를 읽고 deep.md, known-insights.md, next.json
⑥ 게시판         readme-board.cjs    README 최고 기록 표, best/
                 top-board.cjs       insights/TOP.md, render-trees.cjs로 트리 PNG (docs/tree-viewer 사용)
⑦ 다음 실험      next-plan.cjs       next.json 검사·상한 → jobs/auto-*.jsonl
                 start-next.sh       직전 실험 시작 + 150분 이후 시작하도록 start-after.txt와 함께 푸시 → ①
```

## 파일

| 파일 | 하는 일 |
|---|---|
| `run.cjs` | 작업 실행기. 자기 환경(github/kaggle/local) 작업만, 끝난 작업은 건너뜀. 유형: eval, search, ils |
| `lib.cjs`, `jsim.cjs` | 케이스 입력, 답안 검사, 시뮬레이션 평가(웹 채점과 같은 엔진) |
| `search.cjs` | 나무만 바꾸는 국소 탐색. 목표 public(11-15) 또는 robust(501-550) |
| `ils.cjs` | 통로·대수·나무를 함께 바꾸는 반복 국소 탐색(순환 뒤집기, kick) |
| `seeds.cjs` | 팀 시드 규칙: 공개 11-15, 1-500 안 5시드 묶음(선별 4 ⊂ 확인 40 ⊂ 최종 99), 묶음 1-500 |
| `team-gather.cjs` | 전 브랜치 답안 수집·재채점(캐시: team/scores.json) |
| `digest.cjs` | 모든 통계와 기본 보고서 |
| `next-plan.cjs`, `start-next.sh` | AI 제안을 실험 목록으로, 상한·간격 적용 후 푸시 |
| `readme-board.cjs`, `top-board.cjs`, `render-trees.cjs` | README 표, TOP.md, 트리 그림 |
| `usage-log.cjs` | AI 사용량 기록 |
| `summarize-batch.cjs` | 실험 회차의 계획·반환·실패 요약(summary.json) |
| `collect.cjs`, `kaggle-pack.cjs`, `results.cjs` | 로컬 루틴: Kaggle 작업 묶기, 결과 회수 → raw/, caseN/JSM-*.csv |
| `plan3.cjs` | 지정 후보를 시드 단계(screen/confirm/final)로 평가하는 작업 목록 |
| `claude-analysis-prompt.md` | AI 지시문 |
| `team/leaderboard.json` | 저장소에 답안이 없는 리더보드 기록(손으로 적음) |
| `kick.txt`, `kick.md` | 멈춘 루프 다시 시작 |
| `archive/` | 이미 실행이 끝난 일회성 계획 스크립트(B1·B2·overnight). `experiments/common/five-case`에서 `node archive/<파일>`로 실행 |

## 조절하는 값 (저장소 변수, Settings → Secrets and variables → Actions → Variables)

| 변수 | 기본 | 뜻 |
|---|---|---|
| `CLAUDE_MODEL` | opus | 분석 모델 |
| `CLAUDE_EFFORT` | high | 분석 effort |
| `AUTO_MIN_GAP_MIN` | 150 | 실험 시작 간격(분). 5시간 한도에 Opus 분석 약 2번 |
| `AUTO_MAX_PER_DAY` | 15 | 24시간 실험 상한 |

시크릿: `CLAUDE_CODE_OAUTH_TOKEN`(구독 토큰), `JSM_PUSH_TOKEN`(다음 실험 푸시용 PAT).
