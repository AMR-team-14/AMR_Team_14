# AGV Dispatching Optimization — Agent Specification (`agent.md`)

> **Role:** This document defines the mandatory constraints, simulator physics, codebase architecture, proven domain findings, and anti-overfitting rules for any AI agent performing metaheuristic/optimization work in `c:\logistic_final_project`.

---

## 1. 프로젝트 목표 및 목적함수 (Objective & Sensitivity)

* **최소화 목적함수 (시간당 총비용, 천원/시간):**
  $$C = c_A n + c_W \lambda \overline{W} + c_U U_{95} = 15n + 102.24\overline{W} + 100U_{95}$$
  * $c_A = 15$, $c_W = 0.6$, $\lambda = 284 \times \alpha = 284 \times 0.6 = 170.4\text{ 건/시간}$ ($2.84\text{ 건/분}$), $c_U = 100$
  * $\overline{W}$: 워밍업($a_r \ge 300\text{분}$) 이후 도착하여 $T = 3,000\text{분}$ 이내 픽업 완료된 전체 요청 $R$의 평균 총 대기시간 $(p_r - a_r)$
  * $U_{95}$: $R$ 중 긴급 요청($d_r = \text{SHIP}$, 즉 `urgentDest = 6`)의 총 대기시간 $(p_r - a_r)$ 정렬 배열의 선형 보간 95백분위수 (`pos = 0.95 * (N_u - 1)`)
* **비용 민감도:**
  * AGV 대수 1대 증감: $\Delta n = \pm 1 \Rightarrow \pm 15.00$
  * 전체 평균 대기 $0.01\text{분}$ 단축: $\Delta \overline{W} = -0.01 \Rightarrow -1.0224$
  * 긴급 $U_{95}$ $0.01\text{분}$ 단축: $\Delta U_{95} = -0.01 \Rightarrow -1.0000$
* **현재 기록 및 갱신 목표:**
  * **현재 웹 실측 1위 (리더보드 기존 1위 `1,198.16`을 `-12.74점` 돌파):**
    * `answer_final_best.csv` (`n=19`): **웹 실측 `1,185.42`** | `HELD300(201..500) = 1,191.48` | **`ALL800(201..1000) = 1,191.27 ± 11.18`** (`0/800` 실격)
    * `deep_best_18.csv` (`n=18`): **웹 실측 `1,189.44`** | `HELD300(201..500) = 1,191.04` | `ALL800(201..1000) = 1,191.51 ± 12.58` (`0/800` 실격)
    * *(과거 웹 기준선 `robust_best_19`: 웹 `1,201.39` | `ALL800 = 1,201.22 ± 11.90`, 정리 시 삭제됨)*
  * **현재 최적성 지위 및 차기 목표:** `answer_final_best.csv`는 **노드 1개 변경 기준 ε-국소최적(ε ≈ 1점)** 구간에 위치하며 `30,705`개 다중 구조 탐색(`train100`) 대안들은 `800`시드에서 유의 열세(`t ≥ +2.26`). 추가 개선을 노릴 경우 `2-노드 동시 변경 전수 + 신규 800시드` 기준 유의한 우위(`t < -2`)와 `0/800` 실격을 동시에 통과해야 한다.

---

## 2. 절대 제약조건 및 반려 조건 (Hard Constraints)

* **통로 방향 고정 (`fixedDir`, 33비트):**
  * `dir,000011110000000111100000000110101` (변경 불가)
* **AGV 대수 범위:**
  * $1 \le n \le 30$ (실용 최적 영역: `n ∈ {18, 19}`)
* **배차 결정 트리(Dispatching Tree) 유효성 규칙:**
  1. 노드 수: `1 <= n_nodes <= 9`, 루트는 항상 `id = 1`, 모든 노드 `1..n_nodes`는 루트에서 도달 가능한 트리여야 함 (사이클·고립 노드 금지).
  2. 허용 특성 5종: `urg` (긴급 여부 0/1), `w` (배차 시점 대기시간 $t - a_r$, 분), `de` (빈 차 최단거리, m), `dl` (적재 최단거리, m), `qo` (출발지 P/D 노드의 현재 대기 요청 수, 건).
  3. **경로 내 피처 중복 금지:** 루트(`1`)에서 잎(`0`)으로 내려가는 어떤 단일 경로 위에도 동일 특성이 2회 이상 등장할 수 없다.
  4. **`urg` 경계값 고정:** `urg` 노드의 `threshold`는 반드시 `0.5`여야 한다. 단, 방향(`min` | `max`)은 **둘 다 허용되며 웹 채점기에서 정상 작동이 검증되었다** (`urg,min,0.5`가 핵심 돌파구).
* **실격(반려) 조건 (단 1개 시드라도 위반 시 실격):**
  * 시뮬레이션 도중 전체 대기열 $\max_t |Q(t)| > 300$ (`qcap = 300`)
  * 시뮬레이션 종료 시점($t = 3,000$) 잔여 대기열 $|Q(3,000)| > 30$ (`endQmax = 30`)
* **제출 CSV 정확 포맷 (`answer_final_best.csv` 기준):**
  ```csv
  dir,000011110000000111100000000110101
  fleet,19
  key,1,de,min,65,2,3
  key,2,qo,max,1,0,4
  key,3,dl,min,65,5,0
  key,4,urg,max,0.5,0,6
  key,5,qo,max,1,7,8
  key,6,w,min,2.45,0,0
  key,7,urg,min,0.5,0,0
  key,8,w,min,0.75,9,0
  key,9,urg,max,0.5,0,0
  ```
  * 각 `key` 행 스키마: `key,id,f,min|max,threshold,lo_id,hi_id` (`0` = 잎 노드 종료)

---

## 3. 시뮬레이터 물리 엔진 및 타이브레이커 명세 (Simulator Physics)

* **네트워크 (`agv_case_data (1).json`):**
  * 노드 26개(`0..25`), 단방향 간선 33개(`fixedDir`로 방향 확정), 부서 P/D 노드 7개(`pd = [19..25]`, `depts = ["STOR-B","PICK","PACK","STOR-A","RECV","SORT","SHIP"]`).
  * 교차로 11개(무방향 차수 $\ge 3$): 노드 `{2, 3, 4, 5, 6, 7, 10, 12, 14, 16, 18}`.
* **속도 및 작업시간 (가용률 보정 $\text{scale} = 60 / \text{at} = 60 / 50 = 1.2$ 적용):**
  * 적재 이동 속도: $v_c = 50 \text{ m/min} \to$ 실효 $50 / 1.2 = 41.6667 \text{ m/min}$
  * 빈 차 이동 속도: $v_e = 60 \text{ m/min} \to$ 실효 $60 / 1.2 = 50.0 \text{ m/min}$
  * 상역(`tl`) / 하역(`tu`): 각 $0.5\text{분} \to$ 실효 각 $0.6\text{분}$
  * 교차로 통과 점유: $p = 0.3\text{분} \to$ 실효 $0.36\text{분}$ (노드당 1대, FIFO `leave_t = max(t, inter_free[node]) + 0.36`, 동시 도착 시 이벤트 힙 규칙상 AGV 번호 순)
* **초기 AGV 배치 및 동시 시각 일괄 처리:**
  * 0-indexed 차량 `aid` (`0..n-1`)는 `pd[aid % 7]`에서 유휴 상태로 시작 (1-indexed AGV `k` → `pd[(k-1) % 7]`).
  * 동일 시각($|\Delta t| < 10^{-9}$, 타임스탬프 `round9`)의 모든 도착·이동·상하역 이벤트를 일괄 처리한 뒤, 유휴 차량 집합 $I(t)$와 대기 요청 집합 $Q(t)$가 빌 때까지 $\arg\text{lexmin}_{v \in I(t), r \in Q(t)} \kappa(v, r)$를 반복 배차한다. (실제 시드 11 배차 로그 260연속 이벤트 무오차 검증 완료.)
* **배차 키 $\kappa(v, r)$ 평가 및 자동 타이브레이커:**
  * 루트 `1`부터 `nid != 0`인 동안 `is_hi = (val > th)` 판정:
    * 분기: `val <= th` → `lo_id`, `val > th` → `hi_id`
    * 우선순위 비트: `min`이면 `lo`에 `0`, `hi`에 `1`; `max`이면 `hi`에 `0`, `lo`에 `1` 부여 (좌정렬 사전식 비교)
  * 잎(`0`) 도달 후 비트 동률 시 자동 폴백 순서: **1) 빈 차 거리 `de` 짧은 순 (STTF) → 2) 요청 도착순 `rid` 작은 순 → 3) AGV 번호 `aid` 작은 순**.
* **평가 시드 및 로컬-웹 PRNG 주의사항:**
  * 웹 리더보드는 공개 시드 5개(`[11, 12, 13, 14, 15]`, 각 `T=3,000분`, `warm=300분`) 평균으로 채점하고, 최종 성적은 비공개 시드로 채점한다.
  * **중요:** 로컬 `Mulberry32(12..15)`는 웹 채점기의 시드 `12..15` PRNG와 다르다(`agv_dispatch_log_seed11 (1).csv` 도착 8,453건 vs `Mulberry32(11)` 8,531건). 또한 시드 11 CSV는 `arrive`가 소수점 3자리로 반올림되어 있어 나비효과로 단일 시드당 평균 `±15.6점` 분산이 발생한다. 따라서 **로컬 `pub5`는 참고용일 뿐이며, 최적화 및 의사결정은 반드시 대규모 독립 포아송 시드(`train100` / `HELD300`) 기준으로 수행해야 한다.**

---

## 4. 현재 워크스페이스 파일 구조 및 실행·빌드 방법 (Codebase Map)

> **엔진 정책 (C++20 전용):** Python GIL 및 인터프리터 속도 한계로 모든 Python 최적화 스크립트는 제거되었다. 모든 시뮬레이션·검증·최적화는 **독립형 C++20 멀티스레드 엔진 (`agv_opt.cpp`)** 으로 수행한다. (임시 탐색용 C++ 코드는 워크스페이스 루트를 오염시키지 말고 scratch 디렉터리에 작성한다.)

### 4.1 워크스페이스 루트 파일 (`c:\logistic_final_project\`)
| 파일 | 역할 |
| :--- | :--- |
| `agv_opt.cpp` | 독립형 C++20 시뮬레이터·검증기·최적화기 소스 (JSON/CSV 직접 파싱, `COMP_PATH` 경로 압축, `w min` $O(\log Q)$ 이분탐색 지배성, `--verify` 브루트포스 비교, `parallel_for` 멀티스레드) |
| `agv_opt.exe` | `-O3 -std=c++20 -s -Wall -Wextra` 스트립 빌드 실행 바이너리 |
| `agv_case_data (1).json` | 26노드 네트워크, 9개 플로우, 속도/시간/비용/고정 `fixedDir` 입력 JSON |
| `agv_dispatch_log_seed11 (1).csv` | 웹 시뮬레이터 실제 시드 11 배차 로그 (`8,453`건 도착 스트림 추출용) |
| `answer (1).csv` | 과제 원본 기본안 (`n=18`, `de min 75` 1노드) |
| **`answer_final_best.csv`** | **최종 제출 1순위 (`n=19`, 웹 `1,185.42`, `ALL800 = 1,191.27 ± 11.18`, `0/800` 실격)** |
| `deep_best_18.csv` | 18대 웹 검증 대안 (`n=18`, 웹 `1,189.44`, `ALL800 = 1,191.51 ± 12.58`, `0/800` 실격) |
| `agent.md` | 에이전트용 절대 제약·물리 명세·도메인 지식·과적합 방지 명세서 |
| `team_share/` & `team_share_package.zip` | 팀 공유 Git 저장소(`team_share/.git` 보존 필수) 및 배포용 압축팩 |

### 4.2 빌드 및 로컬 검증 CLI 커맨드 (PowerShell, CWD = `c:\logistic_final_project`)
```powershell
# 1. 경고 0건 스트립 빌드 (항상 -s 포함해 바이너리 240KB 수준 유지)
g++ -O3 -std=c++20 -s -Wall -Wextra -o agv_opt.exe agv_opt.cpp

# 2. 공개 5시드 / 100시드 / 미접촉 300시드 / 미접촉 800시드 평가
.\agv_opt.exe --eval answer_final_best.csv
.\agv_opt.exe --eval100 answer_final_best.csv
.\agv_opt.exe --eval300 answer_final_best.csv
.\agv_opt.exe --eval800 answer_final_best.csv

# 3. 제약조건(dir, fleet, tree) + 브루트포스 15스트림 정합성(0.000000000) 검증
.\agv_opt.exe --verify answer_final_best.csv

# 4. 대수 민감도(n=16..23), 노드 Ablation, 경계값 민감도 전체 분석
.\agv_opt.exe --analyze answer_final_best.csv

# 5. 기존 CSV 국소 좌표 미세 탐색 및 GP 구조 재구성 탐색
.\agv_opt.exe --polish answer_final_best.csv polished_out.csv
.\agv_opt.exe --reconstruct answer_final_best.csv
```

### 4.3 로컬 시뮬레이터 재현 기준점 (Parity Checkpoints)
엔진 수정 후 아래 기준점 출력이 소수점 자리까지 일치해야 한다:
* `answer_final_best.csv` (`n=19`, 웹 `1,185.42`):
  * `--eval`: `5-seed: 1178.3137 1190.3025 1189.8866 1200.0376 1196.3056 | mean=1190.9692`
  * `--eval100`: `seed11=1178.31 | pub5=1190.97 | u50(101..150)=1190.57 (dq=0) | held50(151..200)=1188.85 (dq=0) | all100=1189.71`
  * `--eval800`: `HELD300=1191.48 | NEW500=1191.15 | ALL800=1191.27 +/- 11.18 (w=3.7887, u95=5.1891) | dq=0/800`
  * `--verify`: `max |fast - brute| across 15 streams = 0.000000000 (PASS)`
* `deep_best_18.csv` (`n=18`, 웹 `1,189.44`):
  * `--eval`: `5-seed: 1192.8427 1186.0553 1202.2875 1199.5187 1203.2601 | mean=1196.7929`
  * `--eval100`: `seed11=1192.84 | pub5=1196.79 | u50(101..150)=1189.87 (dq=0) | held50(151..200)=1189.29 (dq=0) | all100=1189.58`
  * `--verify`: `max |fast - brute| across 15 streams = 0.000000000 (PASS)`

---

## 5. 에이전트 탐색 원칙, 도메인 물리 지식 및 과적합 방지 프로토콜

### 5.1 확정된 공장 물리 지식 (재탐색 불필요)
1. **이산 특성 격자:**
   * `de ∈ {0, 25, 50, 65, 90, 115, 140, 155, 180, 205}` (예: `65`와 `70~89`는 100% 동일)
   * `dl ∈ {25, 50, 65, 90, 115}` (`PACK→SORT = 90`, `SHIP→RECV = 115`, 모든 긴급 `SORT→SHIP = 25`)
   * `qo ∈ {1..7}` (`qo > 1`이 압도적 분기점)
2. **부서별 유량 균형 (Flow Balance):**
   * 자급자족 균형 부서: `SORT` (`59 in = 59 out`), `PICK` (`54 = 54`), `PACK` (`53 = 53`)
   * 공차 공급원(Sink): `SHIP (+55/시)`, `STOR-A (+55/시)` (`STOR-A→STOR-B = 65m/1.66분`, `SHIP→RECV/SORT = 115m/3.74분`)
   * 공차 수요처(Source): `RECV (-59/시)`, `STOR-B (-51/시)`
3. **핵심 승리 메커니즘 (`urg min 0.5` + `w min` 이중 필터):**
   * 원거리·단일대기(`de > 65, dl <= 65, qo <= 1`)에서 **`urg min 0.5`** 를 두어 `SORT`로 `115m`(공차 ~4분) 원거리 빈 차를 낭비하지 않고, `PACK→SORT` 하역 직후 도착하는 `de = 0m` 빈 차에 맡긴다 (`+10.0점` 효과).
   * 원거리·다중대기(`qo > 1`)에서는 `w <= 0.75~0.80` 신착 구간만 `urg max 0.5`로 정밀 구출(`n=19`)하거나, 이미 지체된 `w > 0.80` 구간에 `urg min 0.5`를 두어 꼬리 낭비를 차단(`n=18`)한다.
4. **이미 반증된 막다른 길 (컴퓨팅 낭비 금지):**
   * 비-`de` 루트(`urg`, `w`, `dl`, `qo`)는 `de min 65` 루트 대비 `+20 ~ +40점` 열위.
   * `de min 115` 루트는 `+30점` 열위.
   * `de > 65`에서 `qo`·`w`보다 먼저 `urg max 0.5`를 두면 `+7 ~ +22점` 악화.
   * `de > 65, dl <= 65`에서 `w min`을 `qo max 1`보다 앞에 두면 `+5 ~ +17점` 악화.
   * `n <= 17` (`HELD300 ~ 1,203.8`) 또는 `n >= 20` (`HELD300 ~ 1,199.6`)은 `n ∈ {18, 19}` 대비 열위.

### 5.2 과적합 및 승자 편향(Winner's Curse) 방지 절대 규칙
1. **5시드·50시드 단독 승격 금지:** 5시드 표준오차는 `se5 ≈ 4.9~5.3점`이며, 50시드에서 수천 개 후보를 선별하면 `+4~5점`의 승자 편향이 발생한다(실증 사례: `reconstruct_best_19`는 `u50 = 1,196.20`이었으나 `HELD200 = 1,201.63`, 웹 `1,206.63`).
2. **필수 3단계 학습·검증 분리 프로토콜:**
   * **1단계 스크리닝:** 공통 난수 `30`시드(기존 12~16시드 대비 노이즈 저감) + 조기 혼잡 컷오프(`q_cut = 65~80`)로 고속 필터링.
   * **2단계 학습/미세탐색:** **`train100` (`Mulberry32` 시드 `101..200`)** 기준 최적화 (`w` 경계값 최소 해상도 `0.05분` 유지 — `2.537` 같은 비현실적 3자리 소수점 튜닝 금지).
   * **3단계 최종 승격 게이트:** 기존 `HELD300/500 (201..1000)` 및 완전 신규 비오염 시드(`1001..1800`)에서 기존 최강(`answer_final_best.csv`: `ALL800 = 1,191.27 ± 11.18`) 대비 통계적으로 유의한 우위(`t < -2`)와 **`0/800` 실격**을 통과한 후보만 루트 CSV로 승격한다.
3. **엔진 무결성 및 워크스페이스 청결 규칙:**
   * `run_sim` 지배성 최적화 또는 새로운 특성 조합 도입 시 반드시 `agv_opt.exe --verify <csv>`로 브루트포스 전체 쌍 탐색과 `max |fast - brute| == 0.000000000` 일치를 검증한다.
   * 중간 실험 CSV·임시 소스는 루트에 쌓지 않고 scratch 디렉터리에서 실행·검증 후 최종 우승안만 루트와 `team_share/`에 반영한다. `team_share/.git`은 절대 삭제·손상하지 않는다.
