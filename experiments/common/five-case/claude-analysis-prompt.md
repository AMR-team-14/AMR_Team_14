AMR Case 3·4·5의 전체 실험 결과를 보고, 케이스마다 이전에는 몰랐던 인사이트만 골라 보고해.
결과는 저장소 루트의 report.md(한국어)에 쓰고, 누적 인사이트 파일
experiments/common/five-case/reports/known-insights.md를 갱신해.

## 0. 이전까지 아는 것
- experiments/common/five-case/reports/known-insights.md를 먼저 읽어. 없으면 처음 실행이니 새로 만든다.
- 이 파일에는 케이스별로 "확인된 인사이트", "검증 필요 가설", "분석에 반영한 결과 출처 목록"이 있다.

## 1. 결과 출처 (전체를 대상으로 하되, 토큰을 아껴서)
- 회수된 회차: experiments/case3·4·5/JSM-*.csv, experiments/common/five-case/raw/*-check.json
- GitHub 실행 보관본: experiments/common/five-case/raw/github/<run_id>/ (batch-summary/summary.json, 샤드별 *-github.jsonl)
- 팀 기록: experiments/common/five-case/team/reported.json, experiments/case3·4·5/insights.md
- 큰 파일은 직접 읽지 말고 node -e 스크립트로 집계해서 결과만 봐.
  필요하면 experiments/common/five-case/results.cjs, analyze.cjs를 써.
- known-insights.md의 출처 목록에 없는 출처가 "새 결과"다.
  새 결과는 자세히 보고, 이전 결과는 비교 기준으로만 집계해.
- 원본 로그 줄은 원인을 확인할 때만 grep으로 뽑아.

## 2. report.md 구성
맨 위: 새로 반영한 출처 목록, 분석 커밋. 새 결과가 없으면 "새 결과 없음" 한 줄과 4절만 쓴다.

### 1) 새 결과 기본 집계 (새 출처만)
- 케이스별 평가 횟수, 누락·실패·반려, 실제 소요 시간 표
- 공개 시드(11-15) 로컬 최저점과 팀 기록의 차이
- 별도 시드 평균과 95백분위는 따로 표시

### 2) 케이스별 새 인사이트 (Case 3 / 4 / 5 각각)
- known-insights.md에 없던 것만 쓴다. 각 항목에 근거(출처·시드 범위·수치)를 붙인다.
- 같은 시드를 쓴 실험끼리만 대수·통로·트리 효과를 비교한다.
- 기존 인사이트를 뒤집거나 강화하는 결과는 "갱신"으로 따로 표시한다.
- 로그로 확인한 원인과 아직 검증이 필요한 설명을 구분한다.
- 새 인사이트가 없는 케이스는 "새 인사이트 없음"이라고만 쓴다.

### 3) 다음 실험 하나
- 가치가 가장 큰 실험 하나. 무엇을 고정하고 무엇을 바꿀지, 판정 기준.

### 4) 현재 최고 기록 한 줄 요약 (케이스별 공개 시드 최저점과 출처)

## 3. known-insights.md 갱신
- 케이스별 "확인된 인사이트", "검증 필요 가설", "분석에 반영한 결과 출처 목록"을 최신 상태로 다시 쓴다.
- 짧게 유지한다(케이스당 10줄 안팎). 뒤집힌 내용은 지우고 최신 결론만 남긴다.

## 제약
- report.md와 known-insights.md 외에는 파일을 수정하지 마.
- 새 실험 실행, 커밋, 푸시, 리더보드 제출은 하지 마. 커밋·푸시는 워크플로가 한다.
- 데이터가 없는 항목은 "데이터 없음"이라고 적고 추측으로 채우지 마.
