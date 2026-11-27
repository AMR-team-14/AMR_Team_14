AMR Case 1~5 실험 결과의 심화 분석만 해(팀 전체 답안 포함). 숫자 계산과 기본 집계 표는 코드가 이미 만들었다(basic.md).
너는 계산하지 말고 해석하고, 다음 실험을 정한다. 결과물은 세 개다.
- deep.md (저장소 루트, 한국어): 심화 분석. 최종 보고서는 basic.md 뒤에 deep.md를 붙인 것이다.
- insights/known-insights.md (저장소 최상단 insights 폴더): 누적 인사이트 갱신.
- next.json (저장소 루트): 코드가 바로 실행할 다음 실험.

## 읽을 것
0. basic.md: 코드가 만든 기본 분석 표. 같은 표를 deep.md에 다시 쓰지 마.
1. digest.json: 모든 숫자의 유일한 출처.
   - sources.new: 이번에 새로 들어온 결과 파일. sources.conflicts: 같은 작업의 점수가 다른 경우.
   - coverage: 회차·환경별 계획/반환/누락(not-collected = 아직 회수 안 함)/실패/반려/평가당 ms.
   - cases.N.publicRanking: 대시보드 순위 — 모든 팀원 브랜치와 jsm 답안을 공통 평가기로 다시 채점한 공개 시드 11-15 순위. owners = 만든 사람.
   - cases.N.robustRanking: 강건성 순위 — 같은 답안들의 시드 1-500 묶음 점수 순위.
   - cases.N.objectives: 공개 1위, 1-500 1위, 같은 답인지(sameAnswer), 둘 다 상위인 답(bestBalanced).
   - cases.N.heldOut / vsBest / screeningSubset / effects / effectsSummary: jsm 실험의 같은 시드 묶음 대응 비교.
   - cases.N.search: 탐색 회차별 결과(ils·search). searchTasks: 탐색 작업 계획/반환.
   - citedEvidence: known-insights.md가 인용한 근거의 현재 판정. recheck이면 그 인사이트를 재검토한다.
2. insights/known-insights.md: 지금까지 확인된 인사이트(네가 갱신). insights/INSIGHTS.md는 팀 전체 총정리(사람이 요청할 때만 갱신)이니 읽기만 하고 고치지 마.
3. 원인을 확인할 때만 원본 로그(experiments/common/five-case/raw/**)를 Grep으로 필요한 줄만 본다. 통째로 읽지 마.

## 채점 기준 — 목표가 둘이다
- Case 2~5: ① 대시보드 = 공개 시드 11-15 점수(publicRanking) ② 강건성 = 시드 1-500 점수(robustRanking). 두 최적은 대개 다른 답이다.
  새 답이 나오면 두 순위에서 각각 몇 위인지, 각 1위를 넘었는지 따로 적는다. 둘 다 상위인 답(bestBalanced)도 본다.
  공개 시드는 고정이라 점수에 잡음이 없다. 공개 점수끼리는 통계 검정 없이 그대로 순위다.
- Case 1: 비공개 시드로 채점하므로 1-500(robustRanking)만 목표다. 공개 점수는 참고.
- 1-500 점수가 좋다고 대시보드 후보로, 공개 점수가 좋다고 강건한 답으로 말하지 마. 목표별로 따로 판단한다.
- 팀원 구분 없이 모든 답안을 하나의 실험 풀로 본다. 팀원끼리 우열을 비교하지 말고 어떤 설계(대수·통로·트리)가 점수를 낮췄는지 본다. 근거가 된 답안은 만든 사람을 적는다(그 사람의 성과).

## 규칙
- 수치는 digest.json에 있는 값만 쓴다. 직접 평균·차이·신뢰구간을 계산하지 마.
- 통계적 주장(더 좋다/나쁘다/같다)에는 근거 id를 [ev:<id>=<verdict>]로 붙인다(digest의 ev·verdict 그대로).
- verdict가 indistinct이면 "구별 안 됨"으로 쓴다.
- 해석·원인 추정은 "확인됨(로그 근거)"과 "가설(검증 필요)"로 구분한다.

## deep.md 구성
맨 위 한 줄: 이번에 새로 들어온 결과 요약(sources.new 기준, 없으면 "새 결과 없음").

### 1) 목표별 현황 (케이스마다 한 줄씩)
- 대시보드 1위 / 강건성 1위가 이번에 바뀌었는지, 바뀌었다면 무엇이 넘었는지(만든 사람 포함).

### 2) 케이스별 새 인사이트 (Case 1 ~ 5)
- known-insights.md에 없던 것만. 뒤집거나 강화하면 "갱신". recheck 항목을 먼저 다룬다. 없으면 "새 인사이트 없음".

### 3) 다음 실험
- 대시보드 1위나 강건성 1위 중 하나라도 넘을 가능성이 가장 큰 실험들을 고른다. 각 실험마다 objective(public|robust)를 정한다.
- 이미 여러 번 돌린 1위 주변 나무 탐색을 반복하지 말고, 아직 덜 본 통로·대수·출발 답안을 우선한다.
- 고정할 것, 바꿀 것, 판정 기준(public: 공개 1위보다 낮고 1-500 반려 0 / robust: 1-500 1위보다 낮음)을 적는다.

## next.json — 다음 실험 명세
```json
{"decision": "run",
 "reason": "한 줄 이유",
 "experiments": [
  {"case": 4, "type": "ils", "start": "sha12:<답안>", "fleet": 18, "budget": 1500, "repeats": 8,
   "objective": "public", "kick": 3, "why": "..."},
  {"case": 5, "type": "ils", "start": "sha12:<답안>", "budget": 600, "repeats": 8, "objective": "robust", "why": "..."},
  {"case": 3, "type": "eval", "sha12": "<답안>", "fleet": 23, "seeds": "public", "why": "..."}
 ]}
```
- ils (권장): 통로·대수·나무를 함께 바꾸는 반복 국소 탐색. 나무 이웃 + 대수 ±1 + 통로 사슬 뒤집기 + 순환 통째 뒤집기로 오르고, 막히면 순환 kick개를 뒤집어 크게 찬 뒤 다시 오른다. Case 1은 통로 고정이라 나무·대수만 바뀐다.
  objective public = 공개 11-15로 오름(평가 1회 약 0.2초), robust = 시드 501-550(1-500 밖)으로 오름(약 1초). budget 최대 3000.
  탐색이 찾은 답은 다음 회차에 자동으로 공개 11-15와 1-500으로 채점되어 두 순위에 들어간다.
- search: 나무만 바꾸는 국소 탐색(통로·대수 고정). {"type": "search", "fleet", "dir": "sha12:..", "start": "transplant:<sha12>", "budget"(≤400), "repeats", "objective"}.
- eval: 기존 답안(fleet만 바꿔도 됨)을 seeds = public | screen | confirm | final(1-500 전부 + 묶음 점수)로 평가.
- sha12는 digest.json에 나온 값만. 한 번에 실험 항목 12개, 탐색(ils+search) 64회까지. GitHub 러너 8대 × 4코어가 동시에 돈다.
  러너 한 대가 약 30분 안에 끝나게 잡는다(예: public ils budget 1500 × 8회 ≈ 러너당 5분).
- 돌릴 가치가 없으면 {"decision": "stop", "reason": "..."}.

## insights/known-insights.md 갱신
- 맨 위 "범용 인사이트"(케이스 공통) 절, 그 아래 케이스별 절. 케이스별로 "대시보드 1위 / 강건성 1위", "확인된 인사이트"(각 줄 끝에 [ev:id=verdict]), "검증 필요 가설", "다음 확인할 것".
- 케이스당 10줄 안팎. 뒤집힌 내용은 지우고 최신 결론만.

## 금지
- deep.md, insights/known-insights.md, next.json 외 파일 수정, 직접 실험 실행, 커밋, 푸시, 리더보드 제출.
- digest.json에 없는 숫자 쓰기. 데이터가 없으면 "데이터 없음".
