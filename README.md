# 물류관리 실험 · jsm

[이전 작업 브랜치](https://github.com/AMR-team-14/AMR_Team_14/tree/jsm-original)와 비교할 수 있다.

- [공통 요구사항](simulator/spec/REQUIREMENTS.md)에서 답안 제약, 시드, 공식 제출 기준을 확인한다.
- [실험 기록](experiments/)의 case1~case5에서 답안과 로그를 읽고, 주요 발견은 해당 케이스의 insights.md에 쓴다.
- [제출 기록](experiments/submissions.csv)은 공식 대시보드에서 확인한 값으로 갱신한다.
- [이전 작업 파일](legacy/)에서 공통 구조에 편입하지 않은 문서와 코드를 찾을 수 있다.

최종 답안은 저장소 루트에서 `node simulator/score.cjs <케이스> <답안 경로> 1 500 --trace`로 평가한다. 새 실험은 `experiments/caseN/<실험ID>/`에 run.json, answer.csv, results.csv, logs/로 저장한다.

[main 반영 계획](experiments/MAIN-%EB%B0%98%EC%98%81%EC%82%AC%ED%95%AD.md)과 [기존 팀 수집본](legacy/team-experiments/README.md)은 이 브랜치에서 확인할 수 있다. 기존 GitHub Pages는 docs/에서 제공한다.

## 다시 쓸 도구

- [배차 트리 뷰어](https://amr-team-14.github.io/AMR_Team_14/)에서 Case 1의 트리를 보고 로컬 평가값을 확인한다. [뷰어 원본](docs/index.html)은 GitHub Pages에서 배포한다.
- [뷰어 입력 만들기](tools/prepare-viewer-data.mjs)는 Case 1 입력과 공식 배차 로그로 뷰어 데이터를 갱신한다. 저장소 루트에서 `node tools/prepare-viewer-data.mjs <케이스 JSON> <시드 11 로그 CSV> docs`를 사용한다.
- [main 반영 스킬](.agents/skills/amr-main-handoff/SKILL.md)은 팀이 공유하기로 한 결과·답안·공통 평가기만 골라 반영할 때 사용한다.
