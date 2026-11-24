# AGV/OHT Multi-Case Dispatching & Aisle Direction Optimization — Agent Specification (`agent.md`)

> **Role:** This document defines the mandatory schema rules, hard constraints, simulator physics, unified C++20 + OpenMP architecture (`agv_solver.cpp`), two-stage optimization methodology, and anti-overfitting protocol for the 5-case AGV/OHT optimization project (`c1.json` ~ `c5.json` $\to$ `ans_c1.csv` ~ `ans_c5.csv`) in `c:\logistic_final_project`.

---

## 1. 프로젝트 개요 및 목적함수 (Objective & 5-Case Summary)

* **목표:** 5개의 케이스 JSON 파일(`c1.json` ~ `c5.json`)을 동적으로 파싱하여 각 공장의 시간당 총비용($C$, 낮을수록 우수)을 최소화하는 최적의 **`(dir, fleet, key)`** 를 산출하고 제출용 CSV(`ans_c1.csv` ~ `ans_c5.csv`)를 생성한다.
* **최소화 목적함수 (시간당 총비용, 천원/시간):**
  $$C = c_{\text{AGV}} \cdot n + c_W \cdot \lambda \cdot \overline{W} + c_U \cdot U_{95}$$
  * $\lambda = \alpha \sum_{k} \text{flows}[k][2]$ (건/시간), 기본 $c_{\text{AGV}} = 15$, $c_W = 0.6$, $c_U = 100$, $\alpha = 0.6$.
  * $\overline{W} = \frac{1}{|R|} \sum_{r \in R} (p_r - a_r)$: 워밍업($a_r \ge 300\text{분}$) 이후 도착하여 $T = 3,000\text{분}$ 이내 픽업 완료($p_r \le 3,000$)된 전체 요청 $R$의 평균 총 대기시간.
  * $U_{95} = Q_{0.95}\{(p_r - a_r) : r \in R_U\}$: $R$ 중 긴급 요청($d_r = \text{urgentDest}$) 대기시간 정렬 배열의 선형 보간 95백분위수(`pos = 0.95 * (N_u - 1)`).
* **5대 케이스 구조 및 현재 검증된 최종 기록 (`ans_c1.csv` ~ `ans_c5.csv`, 미학습 CRN `3,600`시드 `3001..6600` 검증):**

| 케이스 | 제목 / 네트워크 특성 | $V / E / S / F$ | $\lambda$ (건/시) | `urgentDest` (비중) | 대수 범위 $[n_{\min}, n_{\max}]$ | `dir` 제약 (`parallelLanes`) | 교차로 / 복도(Corridor) | 최종 $n$ | **`val3600` Mean ± SD** (`3001..6600`) | **`P95` / `P99` / `Max`** | **`J_risk`** | 공개 5시드 (`11..15`) | 실격 (`val`/`pub`) |
|---|---|---:|---:|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| **`c1`** | 고정 통로 기본 | `26/33/7/9` | `170.4` | `6` (`20.8%`) | `[1, 30]` | **`fixedDir` 고정** (`0`) | `11 / 18` | **`19`** | **`1,193.35 ± 11.63`** | `1,212.71` / `1,221.53` / `1,231.41` | **`1,198.39`** | `1,195.32` | `0/3600` · `0/5` |
| **`c2`** | 격자 기본 (Grid) | `28/36/8/24` | `96.6` | `7` (`8.1%`) | `[1, 30]` | 자유 강연결 (`0`) | `13 / 21` | **`26`** | **`633.54 ± 24.59`** | `660.34` / `714.14` / `847.01` | **`641.36`** | `637.71` | `0/3600` · `0/5` |
| **`c3`** | OHT형 루프 (OHT) | `28/31/10/25` | `96.6` | `8` (`31.1%`) | `[1, 30]` | 자유 강연결 (`0`) | `6 / 9` *(전수)* | **`20`** | **`1,001.09 ± 16.54`** | `1,028.91` / `1,040.13` / `1,061.02` | **`1,008.31`** | `1,011.04` | `0/3600` · `0/5` |
| **`c4`** | 교차로 고밀도 | `46/76/10/22` | `70.2` | `2` (`32.5%`) | **`[1, 25]`** | 자유 강연결 (`0`) | `32 / 62` | **`16`** | **`798.86 ± 7.99`** | `814.75` / `824.90` / `844.50` | **`802.83`** | `797.88` | `0/3600` · `0/5` |
| **`c5`** | 2-존 병목 + 불균형 | `24/30/8/19` | `103.2` | `5` (`28.5%`) | `[1, 30]` | 자유 강연결 (**`1`: `e20,e21`**) | `10 / 15` *(전수)* | **`22`** | **`814.41 ± 21.48`** | `854.60` / `894.10` / `913.00` | **`824.62`** | `821.45` | `0/3600` · `0/5` |

---

## 2. 절대 제약조건 및 반려 조건 (Hard Constraints)

모든 케이스 파라미터는 하드코딩 없이 JSON에서 동적으로 파싱하여 적용해야 한다.

1. **통로 방향 (`dir`, 길이 $E$ 비트열):**
   * `edges[e] = [u, v, dist]`에 대해 `dir[e] == '0'`이면 `u -> v`, `dir[e] == '1'`이면 `v -> u` 단방향 간선.
   * `fixedDir`이 비어 있지 않은 경우(`c1.json`): `dir`은 반드시 `fixedDir`과 일치해야 한다.
   * `fixedDir`이 빈 문자열인 경우(`c2`~`c5.json`): 모든 정점 쌍 간 도달 가능한 **강연결(Strongly Connected, Tarjan/Kosaraju SCC == 1)**이어야 한다.
   * **평행 차선 동시 반전 제약 (`parallelLanes > 0`, `c5.json`):** `edges` 내 `(u, v)`와 `(v, u)`가 동시에 존재하는 간선 쌍(`e1`, `e2`)은 반드시 **`dir[e1] == dir[e2]`** 를 만족해야 하며 탐색 시 동시에 반전한다 (`c5.json`의 브릿지 `edges[20]=[3,8,30]`, `edges[21]=[8,3,30]`).
2. **AGV 대수 (`fleet`):**
   * 반드시 각 JSON의 `[nMin, nMax]` 범위 내 정수여야 한다 (특히 **`c4.json`은 `nMax = 25`** 임에 유의).
3. **배차 결정 트리 (`key`) 엄격 유효성 규칙:**
   * 노드 수 `1 <= n_nodes <= maxNodes` (기본 `9`), 루트는 항상 `id = 1`, 리프 자식은 `0`.
   * **엄격 트리(Strict Tree) 강제:** 루트(`1`)는 진입 차수 `0`, 나머지 모든 노드(`2..n_nodes`)는 진입 차수 정확히 `1`이어야 한다. (웹 채점기는 트리를 재귀적으로 펼쳐 노드 수를 세므로, 노드를 공유하는 DAG는 펼친 노드 수가 9개를 초과하여 `"기준은 9개까지다"` 오류로 반려된다.)
   * **허용 특성 5종:** `urg` (`threshold = 0.5` 고정, `min` | `max` 모두 허용), `w` (대기시간 $t - a_r$), `de` (빈 차 최단거리), `dl` (적재 최단거리), `qo` (출발 스테이션의 현재 대기 요청 수).
   * **경로 내 피처 중복 금지:** 루트(`1`)에서 리프(`0`)로 가는 어떤 단일 경로 위에도 동일한 특성이 2회 이상 등장할 수 없다.
4. **시뮬레이션 실격(반려) 조건:**
   * 시뮬레이션 중 어느 시점이든 전체 대기열 크기 $|Q(t)| > \text{qcap}$ (`300`)이거나, 종료 시점($t = 3,000$) 잔여 대기열 $|Q(3,000)| > \text{endQmax}$ (`30`)이면 해당 시드는 즉시 실격 처리된다.
   * 최종 채택 해는 **공개 시드 5개(`11..15`) 및 검증 시드 500개(`2001..2500`) 모두에서 실격률 0%(`0/5`, `0/500`)**여야 한다.

---

## 3. 시뮬레이터 물리 엔진 및 타이브레이커 명세 (Simulator Physics)

* **교차로 정의:** 원본 무방향 그래프(모든 `edges`를 양방향으로 간주)에서 **무방향 이웃 정점 수(degree)가 3 이상인 정점**을 교차로(Junction)로 정의한다 (`c1: 11`, `c2: 13`, `c3: 6`, `c4: 32`, `c5: 10` — JSON `meta.junctions`와 100% 일치).
* **속도 및 시간 스케일링 ($\text{scale} = 60.0 / \text{at} = 1.2$):**
  * 빈 차 속도 $v_{e,\text{eff}} = \text{ve} / \text{scale} = 50.0\text{ m/min}$, 적재 속도 $v_{c,\text{eff}} = \text{vc} / \text{scale} = 41.6667\text{ m/min}$
  * 상역 $t_{\text{load}} = \text{tl} \times \text{scale} = 0.6\text{분}$, 하역 $t_{\text{unload}} = \text{tu} \times \text{scale} = 0.6\text{분}$
  * 교차로 통과 점유 $t_{\text{inter}} = p \times \text{scale} = 0.36\text{분}$ (노드당 1대 FIFO `leave_t = max(t, inter_free[node]) + 0.36`, 동시 도착 시 `agv_id` 오름차순)
* **최단경로 및 경로 압축 (`comp_path`):**
  * 현재 `dir` 기준 Dijkstra 최단경로로 이동하며, 거리 동률 시 직전 정점 인덱스 오름차순으로 결정적 타이브레이킹한다.
  * 비교차로 중간 정점들의 이동 시간은 하나의 `Hop{교차로 또는 도착 스테이션, 누적 dt}`으로 사전 압축하여 시뮬레이션 속도를 높인다.
* **초기 배치 및 배차 타이브레이커:**
  * 0-indexed AGV `aid` (`0..n-1`)는 스테이션 `pd[aid % S]`에서 유휴 상태로 시작한다.
  * 동일 시각($|\Delta t| < 10^{-9}$, `round9`) 이벤트를 모두 처리한 후 유휴 차량 또는 대기열이 빌 때까지 $\arg\text{lexmin} \kappa(v, r)$를 반복 배차한다.
  * 리프(`0`) 도달 후 비트 동률 시 자동 타이브레이커: **1) `de` 오름차순(STTF) $\to$ 2) 도착 시각 `a_r` 오름차순 $\to$ 3) 요청 번호 `rid` 오름차순 $\to$ 4) 차량 번호 `aid` 오름차순**.

---

## 4. 워크스페이스 파일 구조 및 CLI 실행 방법 (Codebase Map)

### 4.1 워크스페이스 루트 파일 (`c:\logistic_final_project\`)
| 파일 | 역할 |
| :--- | :--- |
| `agv_solver.cpp` | `c1`~`c5` 통합 C++20 + OpenMP 시뮬레이터, Stage A(`dir`) & Stage B(`fleet, key`) 최적화기, 500시드 검증기 소스 |
| `agv_solver.exe` | `-O2 -fopenmp -std=c++20 -Wall -Wextra -s` 빌드 실행 바이너리 |
| `c1.json` ~ `c5.json` | 5대 케이스 입력 JSON 파일 |
| **`ans_c1.csv` ~ `ans_c5.csv`** | **5대 케이스 최종 검증 완료 제출용 CSV 파일** |
| `best_case_1.json` ~ `best_case_5.json` | 5대 케이스별 3,600시드 위험회피 검증 체크포인트 JSON |
| `optimization.log` | 자율 최적화 탐색 및 3,600시드 최종 검증 상세 로그 |
| `agent.md` | 통합 에이전트 명세서 (본 문서) |
| `team_share/` | 팀 공유용 Git 저장소 (`team_share/.git` 보존 필수) |

### 4.2 빌드 및 실행 CLI 커맨드 (PowerShell, CWD = `c:\logistic_final_project`)
> **빌드 주의사항 (Windows MinGW OpenMP):** Windows MinGW GCC 16.2.0에서 `-O3 -march=native`와 `-fopenmp`를 함께 쓰면 OpenMP 워커 스레드 스택(16바이트 정렬)에서 32바이트 정렬 AVX 명령(`vmovdqa`) 충돌(`0xC0000005`)이 발생할 수 있으므로 반드시 **`-O2 -fopenmp -std=c++20 -Wall -Wextra -s`** 로 빌드한다.

```powershell
# 1. 경고 0건 빌드
g++ -O2 -fopenmp -std=c++20 -Wall -Wextra -s -o agv_solver.exe agv_solver.cpp

# 2. Step 1: 전 케이스 JSON 파싱, 교차로/강연결/sumFD_z0 및 c1 기본안 정합성 검증
.\agv_solver.exe --step1

# 3. 전체 또는 개별 케이스 최적화 실행 (ans_c1.csv ~ ans_c5.csv 생성)
.\agv_solver.exe --solve-all
.\agv_solver.exe --solve c3
.\agv_solver.exe --solve c1 custom_out.csv

# 4. 생성된 ans_c1.csv ~ ans_c5.csv 전체 제약조건 + 브루트포스(0.000000000) 고속 검증
.\agv_solver.exe --verify
.\agv_solver.exe --verify c5 ans_c5.csv

# 5. 공개 5시드 + 검증 500시드(2001..2500) 정밀 평가
.\agv_solver.exe --eval
.\agv_solver.exe --eval c2 ans_c2.csv
```

---

## 5. 2단계 통합 최적화 아키텍처 및 과적합 방지 프로토콜

1. **학습/검증 시드 완전 격리 (Zero Data Leakage):**
   * 공개 시드 `[11, 12, 13, 14, 15]`는 탐색(Stage A/B) 중 절대 참조하지 않는다.
   * **Stage B 학습 시드:** 자체 생성 CRN 시드 **`1001 ~ 1200` (`200`개)** 만 사용.
   * **Step 3 최종 선정 게이트:** 미접촉 검증 CRN 시드 **`2001 ~ 2500` (`500`개)** 및 공개 시드 `11..15`에서 **실격 `0`건(`dq == 0`)**을 모두 통과한 후보 중, 강건성 지표 **`Robust_Score = Mean_500 + 0.2 * (P95_500 - Mean_500)`** 최소 해를 최종 선택한다.
2. **Stage A (`dir` 최적화 — 차수-2 복도 축약 + 수송 LP 대리 함수):**
   * 차수-2 정점 사슬 전체를 원자적 복도(`Corridor`)로 축약하고 평행 차선 쌍을 묶어 강연결 파괴를 방지한다.
   * 자유 복도 수 $C \le 16$ (`c3`: 9개 $\to$ 유효 SCC `54`개, `c5`: 14개 $\to$ 유효 SCC `324`개)인 경우 **전역 전수 탐색**을 수행하고, $C > 16$ (`c2`: 21개, `c4`: 62개)인 경우 **1-복도 / 2-복도 / 방향 사이클 반전(Directed Cycle Reversal) 다중 시작점 SA + Tabu**로 탐색하여 해밍 거리 다양성이 보장된 상위 $K = 15$개 `dir` 후보를 추출한다.
   * 대리 목적함수 $J_{\text{surr}}(\text{dir})$는 ① 적재 이동량 $\sum f_k \delta(o_k, d_k)$, ② 잉여-부족 스테이션 간 **정확한 수송 LP 최소비용 공차 유량(Min-Cost Flow)**, ③ 확률적 공차 이동 및 긴급 출발지 접근성, ④ 교차로 통과 부하 제곱합 $\sum_{v \in \text{Junc}} \text{load}(v)^2 + \text{peak}^2$를 결합한다.
3. **Stage B (`fleet, key` 최적화 — 분위수 동적 팔레트 + Beam + LNS + 200시드 폴리시):**
   * 각 `dir`의 최단거리 행렬과 기준 시뮬레이션 대기시간 분포에서 `de`, `dl`, `w` 분위수 및 트리아지 경계값을 동적으로 생성한다 (`urg min 0.5` 및 `de max` 등 양방향 극성 전부 포함).
   * STTF 대수 스윕으로 유망 대수 후보를 좁힌 뒤, Beam Search(`1 -> 9` 노드) + LNS-2/3 파괴·재성장 + 200시드 1D/2D `w` 폴리시 + 주변 대수(`n-1, n, n+1`) 재조정을 거쳐 Step 3 검증 풀에 전달한다.
