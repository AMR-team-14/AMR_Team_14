---
name: amr-main-handoff
description: Prepare a targeted main-branch pull request for AMR Team 14 using agreed case summaries, adopted answers, submission counts, or the shared simulator.
---

# AMR 팀 결과를 main에 반영하기

저장소 루트의 `experiments/MAIN-반영사항.md`에서 이번에 반영할 파일과 경로를 확인한다.

1. 최신 `main`을 기준으로 작업하고, 합의된 결과가 담긴 팀원 브랜치의 커밋을 고정한다. 팀원별 기록은 `experiments/caseN/`과 `experiments/submissions.csv`에서 찾는다.
2. 케이스별 색인에는 답안과 실행 조건, 결과, 원본 커밋 링크를 남긴다. 동일 실행을 판별하려면 케이스, 트리 전체, 차량 대수, 방향, 입력, 평가기·옵션, 집계 방식, 시드를 대조한다. 평가 조건을 모르면 미확인으로 기록한다.
3. 팀 결론과 채택 답안은 근거가 있는 항목만 옮긴다. 공식 제출 횟수는 대시보드로 확인한 값과 확인 시각을 옮긴다. 원시 로그의 위치는 고정 링크로 기록한다.
4. 공통 평가기를 반영하는 경우 `simulator/` 전체를 같은 커밋에서 가져온다. 공개 시드 11~15에서 Case 1~5 기준안을 실행하고 `simulator/internal/fleet-pilot.csv`와 대조한다.
5. 개인 브랜치의 전체 이력을 병합하지 말고 반영 목록에 해당하는 파일만 가져온다. `main` 대비 변경 파일과 내용을 확인한다. `legacy/`, 원시 로그, 개인 실험 폴더, 작업용 워크플로가 들어갔으면 범위를 바로잡는다. 요청 범위에 따라 반영 목록을 보여주거나 풀 리퀘스트를 준비한다.
