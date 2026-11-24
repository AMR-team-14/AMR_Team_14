# 실험 정리 비교용 브랜치

원본 브랜치는 jinhong-experiments이다. [원본 커밋](https://github.com/AMR-team-14/AMR_Team_14/commit/98597c1a874a5812db7c52e9a2ae01f654a0a400)에서 시작했고, 기존 경로도 보존했다.

- [실험 전 요구사항](simulator/spec/REQUIREMENTS.md)
- [실험 기록](experiments/)의 case1~case5와 각 insights.md
- [공식 제출 기록](experiments/submissions.csv)을 대시보드에서 확인해 작성

최종 후보는 저장소 루트에서 node simulator/score.cjs CASE ANSWER.csv 1 500 --trace로 평가한다. 새 실험은 experiments/caseN/<실험ID>/에 run.json, answer.csv, results.csv, logs/로 정리한다.
