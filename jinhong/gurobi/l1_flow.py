# L1 유량 하한: 빈 차 이동을 최소로 하는 수송 LP (Gurobi)
#  - 변수 x[v][o]: 시간당 「v에 내린 빈 차가 출발 부서 o의 주문을 받으러 가는 횟수」
#  - 제약: v에서 나가는 빈 차 = v에 내린 횟수, o로 들어오는 빈 차 = o의 주문 수 (장기 평균에서 반드시 성립)
#  - 목적: 빈 차 이동 시간(자유 주행 + 교차로 통과, 대기 없음)의 합 최소화
#  - 대기 = 주문 발생 → 차가 출발지에 도착. 차는 주문이 생긴 뒤에만 움직이므로 대기 ≥ 빈 차 이동 시간이다.
#    따라서 W̄ ≥ (LP 최적값) / (시간당 주문 수). 교차로 대기 · 차가 모자란 시간은 넣지 않았으므로 느슨한 하한이다.
#  - 강의 55쪽 대수 공식 (16)~(18)에 Le(빈 차 거리)를 넣어 필요 대수도 같이 낸다.
# 실행: python -I gurobi/l1_flow.py   (jinhong 폴더에서, cpp/case.txt 필요)
import json, math, os, sys
sys.stdout.reconfigure(encoding="utf-8")  # python -I는 PYTHONIOENCODING을 무시한다
import gurobipy as gp
from gurobipy import GRB

here = os.path.dirname(os.path.abspath(__file__))
tok = open(os.path.join(here, "..", "cpp", "case.txt"), encoding="utf-8").read().split()
it = iter(tok)
N, nd = int(next(it)), int(next(it))
pd = [int(next(it)) for _ in range(nd)]
D = [[float(next(it)) for _ in range(N)] for _ in range(N)]
NX = [[int(next(it)) for _ in range(N)] for _ in range(N)]
nj = int(next(it)); J = {int(next(it)) for _ in range(nj)}
nf = int(next(it)); F = [(int(next(it)), int(next(it)), float(next(it))) for _ in range(nf)]
urgent = int(next(it)); alpha, vc, ve, tl, tu, at, p, T, warm = (float(next(it)) for _ in range(9))
qcap, endq = int(next(it)), int(next(it)); cA, cW, cU = (float(next(it)) for _ in range(3))
depts = [next(it) for _ in range(nd)]
fac = 60 / at

def njunc(a, b):  # a → b 경로에서 지나는 교차로 수(출발 · 도착 노드 제외)
    c, k = pd[a], 0
    while c != pd[b]:
        c = NX[c][pd[b]]
        if c != pd[b] and c in J: k += 1
    return k

dist = [[D[pd[a]][pd[b]] for b in range(nd)] for a in range(nd)]
te = [[(dist[a][b] / ve + njunc(a, b) * p) * fac for b in range(nd)] for a in range(nd)]   # 빈 차 이동 시간(분)
rate = [(o, d, f * alpha) for o, d, f in F]            # 시간당 건수
lam = sum(f for _, _, f in rate)
inn = [sum(f for o, d, f in rate if d == v) for v in range(nd)]
out = [sum(f for o, d, f in rate if o == v) for v in range(nd)]

def solve(cost, forbid=None, name="L1"):
    m = gp.Model(name); m.Params.OutputFlag = 0
    x = m.addVars(nd, nd, lb=0.0, name="x")
    m.addConstrs((x.sum(v, "*") == inn[v] for v in range(nd)), name="leave")
    m.addConstrs((x.sum("*", o) == out[o] for o in range(nd)), name="serve")
    if forbid:
        for v, o in forbid: x[v, o].UB = 0
    m.setObjective(gp.quicksum(cost[v][o] * x[v, o] for v in range(nd) for o in range(nd)), GRB.MINIMIZE)
    m.optimize()
    if m.Status != GRB.OPTIMAL: return None
    return m.ObjVal, [[x[v, o].X for o in range(nd)] for v in range(nd)]

obj, X = solve(te)
Wlb = obj / lam
Le_min = sum(X[v][o] * dist[v][o] for v in range(nd) for o in range(nd)) / lam
Ld = sum(f * dist[o][d] for o, d, f in rate) / lam
jl = sum(f * njunc(o, d) for o, d, f in rate) / lam
je = sum(X[v][o] * njunc(v, o) for v in range(nd) for o in range(nd)) / lam

def fleet(Le, junc=0.0):  # 강의 (16)~(18): Tc = TL + TU + Ld/vc + Le/ve (+ 교차로 통과), WL = R·Tc, n = WL / AT
    Tc = tl + tu + Ld / vc + Le / ve + junc * p
    WL = lam * Tc
    return Tc, WL, WL / at

print(f"시간당 주문 λ = {lam:.1f}건, 평균 적재 거리 Ld = {Ld:.1f}m")
print("부서별 (내리는 횟수 − 주문 수)/시간:", ", ".join(f"{depts[v]} {inn[v] - out[v]:+.1f}" for v in range(nd)))
print(f"\n[L1] 빈 차 이동 최소 흐름 (Gurobi 최적): 평균 빈 차 거리 Le = {Le_min:.1f}m, 평균 대기 하한 W̄ ≥ {Wlb:.3f}분")
print("  빈 차 흐름(시간당, 0.05 이상만):")
for v in range(nd):
    for o in range(nd):
        if X[v][o] > 0.05: print(f"    {depts[v]:7s} → {depts[o]:7s} {X[v][o]:6.1f}건  ({dist[v][o]:.0f}m, {te[v][o]:.2f}분)")
print("\n[대수 공식, 강의 55쪽] 주기 Tc(분) · 작업량 WL(차·분/시간) · 필요 대수 n = WL / 50")
rows = [("빈 차 거리 최소(L1)", Le_min, 0.0), ("빈 차 거리 최소 + 교차로 통과", Le_min, jl + je),
        ("지금 정책의 실제 빈 차 거리 82m", 82.3, 0.0), ("실제 82m + 교차로 통과(적재 쪽만 근사)", 82.3, jl + je * 82.3 / max(Le_min, 1e-9))]
res = {}
for nm, Le, jc in rows:
    Tc, WL, n = fleet(Le, jc); res[nm] = n
    print(f"  {nm:34s} Tc {Tc:.2f}  WL {WL:7.1f}  n = {n:.2f} → {math.ceil(n)}대")
nmin = math.ceil(fleet(Le_min, jl + je)[2] + 1e-9)
print(f"\n[총비용 하한] 대수 ≥ {nmin}대(작업량), 긴급 95분위 ≥ 0 → C ≥ 15·{nmin} + {cW * lam:.2f}·{Wlb:.3f} = {cA * nmin + cW * lam * Wlb:.1f}")
print("  대수별: " + ", ".join(f"n={n}: {cA * n + cW * lam * Wlb:.0f}" for n in (nmin, 15, 19, 21)))
json.dump({"lam": lam, "Ld": Ld, "Le_min": Le_min, "W_lb": Wlb, "flow": X, "depts": depts, "dist": dist, "te": te, "n_min": nmin},
          open(os.path.join(here, "l1_result.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
