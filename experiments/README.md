# 실험 기록

모든 팀원이 같은 구조로 기록한다. 실험 하나는 폴더 하나다.

~~~text
experiments/
  _template/                 새 실험을 만들 때 복사
  common/                    팀 공통 실험 (대수 조사 등)
  <개인 폴더>/               KMJ, PJH, OJT, YB, JGD, JSM
    root-plan.md             내 계열의 가설과 실험 순서
    <ID>-<짧은 이름>/        예: U01-layer2
      README.md              가설 · 조건 · 결과 · 다음 판단
      run.json               대수 · 시드 · 기준 트리 · 상태
      trees/*.csv            비교할 트리 (key 행만)
      results/               실행기가 채움 (손으로 고치지 않음)
        summary.csv          트리 × 대수별 비용, 반려, 대기, 기준 대비 차이
        raw.jsonl            시드별 전체 결과
        meta.json            엔진 커밋, 시드, 실행 위치, 소요 시간
~~~

- **ID:** 계열 문자 + 두 자리 번호. `D` 거리, `W` 장기 대기, `Q` 적체, `U` 긴급, `L` 적재 거리, `F` 대수 조사, `X` 계열 간 비교, `T` 점검.
- **트리 파일:** `key,id,변수,min|max,임계값,lo,hi` 행만 쓴다. `dir`과 `fleet`은 실행기가 붙인다.
- **run.json:** `fleets`는 대수 목록, `seeds`는 `"1001-1016"`처럼 범위 표기 가능, `baseline`은 짝 비교 기준 트리 파일명이다. `status`는 `planned` → `ready`로 바꾸면 실행된다.
- **결과 읽기:** `deltaVsBaseline`이 양수면 기준보다 싸다. `deltaSE`와 비교해 차이가 작으면 시드를 늘린다. `rejected`가 0이 아닌 행은 비용과 무관하게 탈락이다.
- **최종 후보:** 제출할 CSV만 `solutions/<개인 폴더>/`에 올린다.

## 실행

**GitHub (기본):** `run.json`의 `status`를 `ready`로 바꿔 푸시하면 Actions가 결과 없는 실험을 돌리고 `results/`를 같은 브랜치에 커밋한다. 공개 저장소라 무료이며 결과와 실행 기록이 남는다. 받은 뒤 `git pull`.

**로컬 (빠른 확인):**

~~~sh
node tools/build.mjs native
node tools/run-batch.mjs experiments/JSM/U01-layer2          # 결과가 있으면 --force
~~~

**후보 생성:** 부모 트리의 빈 자식 자리 하나에 규칙을 하나씩 붙인 트리를 모두 만든다. [설계 문서](../docs/notes/experiment-design.md) 4.1절·5절의 자동 적용 규칙으로 무효·중복 후보는 만들지 않는다.

~~~sh
node tools/expand.mjs <부모.csv> <노드번호:lo|hi> <실험 폴더>/trees
~~~
