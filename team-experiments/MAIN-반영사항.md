# main에 반영할 내용

비교용 브랜치는 jkd(kyudam-experiment), kmj(DOE1119), yb(younbean), pjh(jinhong-experiments)다. 개인 기록은 각 브랜치의 experiments/caseN에서 확인할 수 있다.

| main 경로 | 반영할 내용 |
|---|---|
| simulator/ | 공통 score.cjs, 내부 엔진, 고정 입력과 기준 점수표 |
| simulator/spec/ | 고정한 c1.json~c5.json과 REQUIREMENTS.md |
| experiments/caseN/index.csv | 팀에서 공유하기로 한 실험의 조건, 점수, 반려 여부, 원본 커밋 링크 |
| experiments/caseN/insights.md | 팀이 확인한 결론과 근거 |
| experiments/caseN/best-answer.csv | 팀이 채택한 답안 |
| experiments/submission-summary.csv | 팀원별 공식 제출 사용 횟수, 확인 시각, 대시보드 근거 |

원시 로그는 개인 브랜치에 둔다. main에는 결과 요약과 원본 링크를 올린다. 비교용 브랜치에는 기존 실행을 다시 돌릴 때 필요한 원본 경로도 유지했다.

## 실험 순서

1. 공통 평가기가 main에 들어가면 팀원은 origin/main의 simulator/를 자기 브랜치에서 사용하고, run.json에 출처 커밋을 적는다.
2. **실험 중복 확인**에서 본인 기록과 main의 색인을 조회한다. 케이스, 트리 전체, 대수, 통로 방향, 고정 입력, 평가기·옵션, 집계 방식, 시드를 비교한다.
3. simulator/spec/REQUIREMENTS.md에 지정한 파일과 수업 안내를 대조한 뒤 실행한다. 최종 트리는 모두 시드 1~500으로 평가한다. node simulator/score.cjs CASE ANSWER.csv 1 500 --trace로 비용·반려·배정 기록 해시를 남긴다.
4. 결과, 답안, 시드, 로그를 본인 브랜치에 기록한다. 공식 웹 제출을 했다면 experiments/submissions.csv도 갱신한다.

## 공식 제출 횟수

1인당 최대 60회다. 각자 대시보드에서 누적 사용 횟수나 남은 횟수를 확인하고, 확인 시각과 근거를 본인 브랜치에 남긴다. 남은 횟수가 표시되면 60에서 빼서 사용 횟수를 확인한다. 점수나 제출 답안은 비교 근거로 기록하고 시드는 대시보드나 내보내기 파일에 표시된 경우 기록한다. 점수만으로 시드는 확정할 수 없다. 실제 수치가 확인되면 팀 공유용 요약에 올린다.
