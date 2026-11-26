AMR Case 3·4·5의 완료된 실험 결과를 읽고 분석해. 결과는 저장소 루트의 report.md 하나에 한국어로 써.

읽는 순서 (집계 먼저, 원본 로그는 원인 확인 때만)
1. artifacts/batch-summary/summary.json — 있으면 이번 GitHub 회차의 계획·반환·누락·실패·비용 집계
2. experiments/common/five-case/raw/*-check.json — 회수가 끝난 회차의 계획·반환·누락·실패·환경별 속도
3. experiments/case3/JSM-*.csv, experiments/case4/JSM-*.csv, experiments/case5/JSM-*.csv
   — 평가 기록 (seeds, score, rejected, status, env, elapsed_ms, raw_log 열)
4. 팀 기록: experiments/common/five-case/team/reported.json, experiments/case3·4·5/insights.md
원인을 확인할 때만 raw_log 열이 가리키는 원본 로그(experiments/common/five-case/raw/*.jsonl, artifacts/jsm-five-case-*/)를 열어.
큰 파일은 통째로 읽지 말고 grep이나 node -e로 필요한 줄만 뽑아.

기본 분석
- 케이스별 평가 횟수, 누락·실패·반려, 실제 소요 시간을 표로 정리해.
- 공개 시드(11-15) 로컬 최저점과 팀 기록의 차이를 보여줘.
- 별도 시드(공개 시드 외 범위)의 평균과 95백분위는 따로 표시해.

심화 분석
- 같은 시드를 쓴 실험끼리만 대수·통로(dir)·트리 변경 효과를 비교해.
  필요하면 node experiments/common/five-case/analyze.cjs <case> <baselineSha12> <screen|confirm|final> 로 대응 비교를 계산해.
- 로그로 확인한 원인과 아직 검증이 필요한 설명을 구분해.
- 다음에 돌릴 가치가 가장 큰 실험 하나를 골라. 무엇을 고정하고 무엇을 바꿀지, 판정 기준도 적어.

제약
- 보고서만 써. 새 실험 실행, 파일 수정(report.md 제외), 커밋, 푸시, 리더보드 제출은 하지 마. 커밋·푸시는 워크플로가 한다.
- 데이터가 없는 항목은 "데이터 없음"이라고 적고 추측으로 채우지 마.
- 맨 위에 분석한 회차(batch)와 커밋, 읽은 파일 목록을 한 줄씩 적어.
