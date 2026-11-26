AMR Case 3·4·5 실험 결과의 심화 분석만 해. 숫자 계산과 기본 집계 표는 코드가 이미 만들었다(basic.md).
너는 계산하지 말고 해석만 한다. 결과는 저장소 루트의 deep.md(한국어)에 쓰고,
experiments/common/five-case/reports/known-insights.md를 갱신해. 최종 보고서는 basic.md 뒤에 deep.md를 붙인 것이다.

## 읽을 것
0. basic.md (저장소 루트): 코드가 만든 기본 분석 표. 같은 표를 deep.md에 다시 쓰지 마.
1. digest.json (저장소 루트): 코드가 만든 전체 집계. 모든 숫자의 유일한 출처다.
   - sources.new: 이번에 새로 들어온 결과 파일. sources.conflicts: 같은 작업의 점수가 다른 경우.
   - coverage: 회차·환경별 계획/반환/누락(missingReason: not-collected = 아직 회수 안 함, missing-in-returned-results = 실제 누락)/실패/반려/평가당 ms.
   - cases.N.public: 공개 시드(11-15) 최저점, 팀 기록, 차이.
   - cases.N.heldOut: 별도 시드 후보별 평균·95백분위·adjMean(공통 난수 보정 평균)·비용 분해.
   - cases.N.currentBest / vsBest: 현재 최고와 각 후보의 대응 비교(Holm 보정 p, verdict).
   - cases.N.screeningSubset: 최고와 통계적으로 구별되지 않는 후보 집합.
   - cases.N.effects / effectsSummary: 대수·통로(dir)·트리 하나만 다른 쌍의 효과.
   - cases.N.publicRanking: 모든 팀원 브랜치와 jsm의 답안을 공통 평가기로 다시 채점한 공개 시드 순위(채점 기준). 팀원 구분 없이 하나의 실험 풀이다. s500 = 시드 1-500 묶음 점수(강건성), sources = 답안이 있는 브랜치·파일·커밋.
   - cases.N.ocba: 비공개 시드 채점 케이스(Case 1)에서만 계산하는 시드 배분.
   - cases.N.search: 탐색 회차별 결과(공개 시드 기준). dirLegend: D번호 ↔ 실제 dir.
   - citedEvidence: known-insights.md가 인용한 근거의 현재 판정. status가 recheck이면 그 인사이트를 재검토해야 한다.
2. experiments/common/five-case/reports/known-insights.md: 지금까지 확인된 인사이트. 없으면 처음 실행이다.
3. 원인을 확인할 때만 원본 로그(experiments/common/five-case/raw/**)를 Grep으로 필요한 줄만 본다. 통째로 읽지 마.

## 채점 기준 (판단의 출발점)
- Case 2~5는 공개 시드 11-15 점수가 그대로 성적이다. Case 3·4·5의 목표는 팀 전체 공개 시드 1위(cases.N.publicRanking[0])보다 낮은 답안을 찾는 것이다.
  공개 시드는 고정이라 점수에 잡음이 없다. 공개 점수끼리의 비교는 통계 검정 없이 그대로 순위다.
- 팀원 구분 없이 모든 답안을 하나의 실험 풀로 본다. 누구 것인지 비교하지 말고, 어떤 설계(대수·통로·트리)가 점수를 낮췄는지 본다.
- 별도 시드 결과는 강건성 참고용이다(반려가 없는지, 공개 점수가 우연한 극단값이 아닌지). 별도 시드 평균이 더 좋다는 이유로 공개 점수가 나쁜 후보를 추천하지 마.
- Case 1만 비공개 시드로 채점한다(범위 밖).

## 규칙
- 보고서의 모든 수치는 digest.json에 있는 값만 쓴다. 직접 평균·차이·신뢰구간을 계산하지 마.
- 통계적 주장(더 좋다/나쁘다/같다)에는 반드시 근거 id를 [ev:<id>=<verdict>] 형식으로 붙인다.
  id와 verdict는 digest.json의 ev·verdict 값을 그대로 쓴다(예: [ev:c4.vsbest.03b542a9976c=worse]).
- verdict가 indistinct이면 "구별 안 됨"으로 쓰고, 좋아졌다/나빠졌다고 쓰지 마.
- 해석·원인 추정은 "확인됨(로그 근거)"과 "가설(검증 필요)"로 구분한다.

## deep.md 구성
맨 위 한 줄: 이번에 새로 들어온 결과 요약(digest.sources.new 기준, 없으면 "새 결과 없음").

### 1) 케이스별 새 인사이트 (Case 3 / 4 / 5)
- known-insights.md에 없던 것만 쓴다. 기존 인사이트를 뒤집거나 강화하면 "갱신"으로 표시한다.
- citedEvidence에서 recheck가 뜬 항목은 맨 먼저 다룬다.
- 대수·통로·트리 효과는 effects·effectsSummary로만 말한다.
- 새 인사이트가 없으면 "새 인사이트 없음".

### 2) 다음 실험 하나
- 공개 점수를 팀 기록보다 낮출 가능성이 가장 큰 실험을 고른다(publicRanking, search, coverage의 미회수·미탐색 영역 근거). 1위와 가까운 후보가 많은 케이스, 아직 탐색이 덜 된 케이스를 우선 본다.
- 고정할 것, 바꿀 것, 판정 기준(공개 시드 11-15 점수가 팀 전체 1위보다 낮고, 시드 1-500에서 반려 0)을 적는다.


## known-insights.md 갱신
- 케이스별로 "확인된 인사이트"(각 줄 끝에 [ev:id=verdict]), "검증 필요 가설", "다음 확인할 것"을 쓴다.
- 케이스당 10줄 안팎. 뒤집힌 내용은 지우고 최신 결론만 남긴다.

## 금지
- deep.md와 known-insights.md 외 파일 수정, 새 실험 실행, 커밋, 푸시, 리더보드 제출.
- digest.json에 없는 숫자를 만들어 쓰기. 데이터가 없으면 "데이터 없음".
