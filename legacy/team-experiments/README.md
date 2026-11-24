# 팀 실험 결과 모음
각 케이스의 inventory.csv에서 기존 답안, 점수표, 원시 로그와 실험 메모를 찾을 수 있다. 여러 케이스를 다루거나 파일명만으로 소속을 알기 어려운 자료는 shared에 모았다. 팀원은 쓰던 탐색기를 그대로 사용하고, 최종 후보를 비교할 때 같은 입력과 채점 방식을 쓰면 된다.

## 폴더 한눈에 보기

```text
team-experiments/
├── README.md                  지금 읽고 있는 안내
├── evaluator/
│   ├── score.cjs              내 답안 한 개 채점
│   └── internal/              시뮬레이터, 입력, 검증 자료
├── case1/
│   ├── inventory.csv          기존 기록의 파일명과 원본 링크
│   ├── archive/               수집한 파일 사본
│   └── submissions/           새 결과를 공유할 때 작성자가 생성
├── case2/                     case2~case5도 같은 구조
├── case3/
├── case4/
├── case5/
├── shared/                    여러 케이스에 걸친 자료와 소속 미확정 자료
├── collect.mjs                브랜치 기록 수집
└── collect.yml.template       수동 실행용 GitHub Actions 템플릿
```

## 원하는 결과 찾기
| 열 | 읽는 법 |
|---|---|
| original_path | 원래 파일명과 실험 폴더. summary.csv는 요약, raw.jsonl.gz는 압축된 원시 기록인 경우가 많다. |
| branches | 해당 경로에서 똑같은 파일이 발견된 브랜치. 작성자 명단과는 다를 수 있다. |
| git_object | 원본 내용의 Git 식별자. 바이트가 같은 파일은 한 번 저장한다. |
| local_file | 이 폴더 기준 실제 사본 경로. 대량 후보 trees 폴더는 다음 열로 연다. |
| permalink | 수집 당시 커밋에 고정한 원본 링크. 폴더 링크는 그 안의 후보 전체를 연다. |

## 새 결과를 남길 때
자기 브랜치의 team-experiments/caseN/submissions/에 배치별 결과표를 올리면 수집기가 찾는다. 기존 개인 실험 폴더에 있는 기록도 함께 수집한다. 팀원이나 팀원의 에이전트는 아래 열을 사용한다. 예전 자료는 원래 형식으로 보존한다.

| 열 | 담을 내용 |
|---|---|
| case, batch, author | 케이스 번호, 배치 이름, 실행 담당자 |
| hypothesis | 이 실험에서 확인하려던 질문 |
| direction, fleet, candidate | 통로 방향, 차량 대수, 트리까지 포함한 답안 원본 경로와 해시 |
| input, engine | 입력 파일 해시와 평가기 커밋 |
| seeds, aggregation | 시드 목록과 점수를 합산한 방식 |
| score, rejected | 총비용과 반려된 시드 목록. 반려된 묶음의 비용은 비워 둔다. |
| source | 재현 명령과 원시 로그의 고정 경로 |

## 공통 평가기와 웹 방식 채점

웹 방식은 수업 웹 채점기의 계산 절차를 로컬에서 재현하는 방식이다. 예를 들어 시드 11~15를 돌리면 다섯 실행의 픽업 기록을 모아 평균 대기시간과 긴급 요청 대기시간의 95백분위수를 구한 뒤 차량 비용을 더한다. 시드마다 긴급 요청 수와 대기시간 분포가 다르므로 시드별 점수 다섯 개의 평균과 값이 달라질 수 있다. 이 명령은 공식 제출 횟수를 쓰지 않는다.
evaluator/score.cjs는 진홍 브랜치의 웹 방식 엔진과 고정된 케이스 입력으로 답안 하나를 평가한다. 기존 답안의 dir, fleet, key 행 형식을 그대로 받는다. 저장소 루트에서 아래처럼 실행한다.

    node team-experiments/evaluator/score.cjs 2 answer.csv 11 5
    node team-experiments/evaluator/score.cjs 2 --baseline 11 5

출력의 pooled_cost는 지정한 시드들의 픽업을 합쳐 계산한 비용이다. 반려 시드는 rejected에 표시한다. 기준표는 evaluator/internal/fleet-pilot.csv에 있다. 공식 채점과의 일치 여부는 케이스별 제출 기록으로 따로 확인한다.

## 수집 범위와 호출 시점
collect.mjs는 원격 브랜치의 최신 커밋에서 결과·표·로그·압축 자료·문서 등 지원 확장자의 파일을 수집한다. 파일 내용은 그대로 보존하고, 대량 후보 trees 폴더는 Git 트리 단위로 색인한다. 브랜치에서 삭제된 과거 파일과 GitHub Actions에만 남은 아티팩트는 이 목록의 범위 밖이다. 원본을 실행하지 않는다.

collect.yml.template은 수동 실행용 GitHub Actions 작업이다. 관리자가 기본 브랜치의 .github/workflows/에 설치한 뒤 Run workflow를 눌렀을 때만 브랜치 목록을 갱신하고 jsm의 수집 폴더를 업데이트한다. 언어 모델 호출, 제출, 타 브랜치 병합 기능은 없다.
