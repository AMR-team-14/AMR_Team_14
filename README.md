# 물류관리 팀 프로젝트

공통 평가기로 답안을 비교하고, 팀에서 공유하기로 한 결과와 대표 답안을 이 브랜치에 모은다. 각자의 실험 원본과 원시 로그는 개인 브랜치에서 관리한다.

| 경로 | 내용 |
|---|---|
| [simulator/](simulator/) | 공통 평가기, 고정 케이스 입력, 기준 점수 |
| [실험 요구사항](simulator/spec/REQUIREMENTS.md) | 답안 형식, 케이스별 제약, 반려와 시드 기준 |
| [experiments/](experiments/) | 케이스별 색인(index.csv), 결론(insights.md), 대표 답안(best-answer.csv) |
| [제출 횟수 요약](experiments/submission-summary.csv) | 공식 제출 기록을 확인한 팀원별 합계 |
| [legacy/](legacy/) | 기존 평가기, 체크포인트, 기술 보고서 |

## 실행

저장소 루트에서 Node.js로 채점한다. 예를 들어 Case 2의 대표 답안을 팀 공통 시드 1~500으로 평가하려면 다음을 실행한다.

~~~sh
node simulator/score.cjs 2 experiments/case2/best-answer.csv 1 500 --trace
~~~

중간 탐색에는 필요한 개수만큼 시드 1~N을 쓴다. 새 결과를 공유할 때는 개인 브랜치에 조건·답안·결과를 커밋하고, 해당 케이스의 색인에 결과 요약과 고정 링크를 올린다. 기존 채점 기록과 공통 평가기 점수는 실행 방식과 시드 범위를 보고 비교한다.
