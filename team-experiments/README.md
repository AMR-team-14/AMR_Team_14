# 팀 실험 기록
에이전트에게 이 폴더를 읽히면 된다. 팀원이 배치를 마쳤을 때 결과를 자기 브랜치의 case1~case5 중 해당 폴더에 추가하고, 결과 파일만 담은 풀 리퀘스트(PR)를 main으로 연다. 기존 원시 로그는 원래 위치에 두고 결과에서 커밋과 경로를 가리킨다.

## 왜 함께 기록하나
Case 1에서 진홍과 윤빈이 같은 19대·9노드 답안을 공개 시드 11~15로 평가했다. 웹 방식은 1185.42, 팀 C++ 방식은 1190.9692였다. 요청 생성과 집계 방식이 달라 같은 열에 넣으면 순위 해석이 흔들린다. 윤빈은 기존 트리 18대도 평가했다. 승민의 기존 실행은 별도 중복 건수에서 제외했다. Case 3에서는 민재와 진홍이 루프 방향을 열거했고, Case 5에서는 두 사람 모두 구역 간 재배치를 살폈다. 이 두 사례는 탐색 주제가 겹친다. Case 2·4를 포함해 팀원 전원의 개별 실행이 동일했다는 집계는 아직 없다. 모든 팀원이 직접 평가기를 만들었다는 근거도 없다.

[Case 1 원본 대조](https://github.com/AMR-team-14/AMR_Team_14/blob/younbean/experiments/younbean/submission_validation.md) · [진홍의 케이스별 기록](https://github.com/AMR-team-14/AMR_Team_14/tree/jinhong-experiments/jinhong) · [민재의 방법론](https://github.com/AMR-team-14/AMR_Team_14/blob/main/REPORT_METHODOLOGY_AND_OPTIMALITY.md)

## 공통 평가 방법
팀 비교용 기준으로 진홍의 웹 방식 구현을 제안한다. evaluator 안에 엔진, 고정 입력, 실행 예시, 기존 기준표를 함께 두었다. 임시 사본에서 node pilot.cjs를 실행하면 다섯 케이스의 기준 대수와 ±1대를 평가한다. 생성된 fleet-pilot.csv를 보관본과 비교한다. 새 후보도 같은 요청 생성, 통로 방향, 주문 합산, 반려 판정을 사용한다. 개별 탐색기를 써도 최종 비교표에는 공통 방식으로 재평가한 점수를 올린다. 공식 웹과 일치 여부는 케이스별 기준안부터 확인한다.

## 결과 파일 규약
배치마다 caseN/작성자-배치.json 한 개를 추가한다. 최상위 값은 행 객체 배열이다. 각 행의 필수 필드는 candidate(방향·대수·트리를 포함한 후보의 해시), input(입력 해시), engine(평가기 버전), aggregation(집계 방식), seeds(실행 순서대로 정수 배열), score(유한 숫자 또는 반려 시 null), source(고정 커밋과 원본 경로)다. 동일 케이스에서 앞의 다섯 조건이 같으면 기존 결과를 먼저 확인한다. 이력이 불완전한 옛 기록은 원문으로 인용하고 확인된 값만 비교한다.

## 병합 담당자
merge-agent.yml.template은 수동 호출형 GitHub Actions 작업이다. 팀 합의 뒤 main의 .github/workflows/merge-experiment.yml에 설치한다. 병합할 PR 번호를 지정한 시점에만 동작한다. 같은 저장소의 main 대상 PR에서 새 결과 JSON만 검사한 뒤 검사한 커밋을 병합한다. 충돌이나 보호 규칙으로 실패하면 그 사유를 확인한다. 현재 템플릿에는 예약, 반복 조회, Claude 호출이 없다.
