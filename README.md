# 물류관리 실험 · jkd

[이전 작업 브랜치](https://github.com/AMR-team-14/AMR_Team_14/tree/kyudam-experiment)와 비교할 수 있다.

- [공통 요구사항](simulator/spec/REQUIREMENTS.md)에서 답안 제약, 시드, 공식 제출 기준을 확인한다.
- [실험 기록](experiments/)의 case1~case5에서 답안과 로그를 읽고, 주요 발견은 해당 케이스의 insights.md에 쓴다.
- [제출 기록](experiments/submissions.csv)은 공식 대시보드에서 확인한 값으로 갱신한다.
- [이전 작업 파일](legacy/)에서 공통 구조에 편입하지 않은 문서와 코드를 찾을 수 있다.

최종 답안은 저장소 루트에서 `node simulator/score.cjs <케이스> <답안 경로> 1 500 --trace`로 평가한다. 새 실험은 `experiments/caseN/<실험ID>/`에 run.json, answer.csv, results.csv, logs/로 저장한다.
