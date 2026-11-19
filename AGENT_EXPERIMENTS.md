# 에이전트 실험 가이드

저장소 루트에서 명령을 실행합니다. 탐색 변수는 차량 대수와 배차 트리입니다. 케이스의 통로 방향과 이동 규칙은 고정합니다.

## 준비와 평가

Node.js와 C++20을 지원하는 GCC 또는 Clang을 준비합니다. 컴파일러가 기본 경로에 없으면 `CXX` 환경 변수로 지정합니다. 실험에는 웹 서버나 브라우저가 필요 없습니다.

```sh
node tools/build.mjs native
./build/agv_opt --eval answer_final_best.csv
./build/agv_opt --eval-json answer_final_best.csv
```

Windows에서는 `build\agv_opt.exe`를 실행합니다. 기본 입력은 `docs/case.json`과 `docs/seed11.json`입니다. 같은 케이스 파일을 별도 위치에 뒀다면 `--data-dir <폴더>`를 넘깁니다.

`--eval-json`은 차량 대수, 트리, 시드별 비용, 평균 대기, 긴급 대기 95백분위수, 최대·종료 대기열을 반환합니다. 시드 반려는 `rejected: true`와 `cost: null`로 표시합니다. 한 시드라도 반려되면 `average`도 `null`입니다.

## 후보 탐색과 검증

```sh
./build/agv_opt --analyze answer_final_best.csv
./build/agv_opt --robust --output-dir runs/experiment-01
./build/agv_opt --eval-json runs/experiment-01/answer_final_best.csv --seeds 151,152,153,154,155
```

1. `--analyze`로 대수와 트리 규칙을 바꿨을 때의 점수 변화를 확인합니다.
2. `--robust`는 18·19·20대 후보를 탐색합니다. 기본 출력은 `runs/robust/`입니다. 새 실험에 독립된 폴더를 사용합니다.
3. 탐색에 쓰는 시드는 11~15 및 101~150입니다. 후보 간 비교에 같은 시드를 쓰고, 일반화 성능은 탐색에 쓰지 않은 별도 시드에서 확인합니다. 예제의 151~155는 로컬에서 새로 생성하는 검증 시나리오입니다.
4. 최종 후보는 공식 과제 페이지에서 동일한 파일을 평가합니다. 팀에 공유하는 PR에는 후보 CSV와 사용한 시드, 로컬 평균, 공식 웹 점수를 적습니다.

공식 로그를 복원한 11번 시드와 로컬에서 생성한 시나리오는 공식 서버의 채점 결과와 다를 수 있습니다.
