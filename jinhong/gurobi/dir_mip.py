# 통로 방향 MIP (Gurobi) — 강의 7 「문제 1」(Gaskins & Tanchoco 1987)의 케이스 일반판.
#  - 변수 z[k] ∈ {0,1}: 통로 k의 방향(0 = 데이터에 적힌 방향 a→b, 1 = 반대). 제출 형식 dir 비트열과 같은 뜻이다.
#  - 적재 흐름 x[o][호]: 출발 스테이션 o별 다품종 흐름. 주문 f(o,d)를 pd[o] → pd[d]로 보낸다.
#  - 빈 차 흐름 e[호]: 한 품종. 스테이션 s의 공급 = (도착 주문 수 − 출발 주문 수). 빈 차는 서로 바꿔 쓸 수 있으므로 한 품종이면 된다.
#  - 강연결: 뿌리 노드 0에서 모든 노드로, 모든 노드에서 뿌리로 1씩 보내는 보조 흐름 두 개.
#  - 목적(variant):
#      load  : Σ f·d (적재 이동 거리, 웹 화면의 「적재 이동 Σf·d」와 같은 값)
#      both  : 적재 거리 + we × 빈 차 거리(빈 차가 이상적으로 배정될 때의 최소)
#      time  : 차량 시간 = 적재(거리/vc) + 빈 차(거리/ve) + 교차로 통과(p) × 교차로 진입 횟수
#  - 상위 해 여러 개: PoolSearchMode=2 로 목적값이 좋은 순서대로 pool 개를 모은다.
#  한계: 교차로 대기 · 주문 시각의 엇갈림 · 배차 규칙은 수식에 없다. 그래서 후보는 반드시 시뮬레이션(dirrank.js)으로 다시 순위를 매긴다.
#      time 변형에는 교차로 가중 pm(교차로 대기를 대신하는 배수)과 긴급 가중 wu(긴급 출발지까지 빈 차가 가는 시간)를 줄 수 있다.
# 실행: python -I gurobi/dir_mip.py <케이스 번호 또는 json 경로> <출력 json> [pool=40] [gap=0.08] [ext]
import json, os, sys
sys.stdout.reconfigure(encoding="utf-8")
import gurobipy as gp
from gurobipy import GRB

arg = sys.argv[1]
src = arg if arg.endswith(".json") else os.path.join(os.environ.get("AGV_CASES", "C:/dev/ie325-amr/cases"), f"final-agv-c{arg}.json")
P = json.load(open(src, encoding="utf-8"))
outp = sys.argv[2]
POOL = int(sys.argv[3]) if len(sys.argv) > 3 else 40
GAP = float(sys.argv[4]) if len(sys.argv) > 4 else 0.08

N, E, pd = len(P["nodes"]), P["edges"], P["pd"]
M, nd = len(E), len(pd)
F = [(o, d, f * P["alpha"]) for o, d, f in P["flows"]]          # 시간당 건수
out_ = [sum(f for o, d, f in F if o == s) for s in range(nd)]
in_ = [sum(f for o, d, f in F if d == s) for s in range(nd)]
lam = sum(out_)
deg = [0] * N
for a, b, _ in E: deg[a] += 1; deg[b] += 1
isJ = [d >= 3 for d in deg]
# 호: (k, s) — s=0: a→b, s=1: b→a
arcs = [(k, s) for k in range(M) for s in (0, 1)]
tail = {(k, s): (E[k][0] if s == 0 else E[k][1]) for k, s in arcs}
head = {(k, s): (E[k][1] if s == 0 else E[k][0]) for k, s in arcs}
ln = {(k, s): E[k][2] for k, s in arcs}

def floyd(bits):
    INF = float("inf")
    D = [[0 if i == j else INF for j in range(N)] for i in range(N)]
    for k in range(M):
        a, b = (E[k][1], E[k][0]) if bits[k] == "1" else (E[k][0], E[k][1])
        D[a][b] = E[k][2]
    for k in range(N):
        Dk = D[k]
        for i in range(N):
            dik = D[i][k]
            if dik == INF: continue
            Di = D[i]
            for j in range(N):
                if dik + Dk[j] < Di[j]: Di[j] = dik + Dk[j]
    return D

def build(variant, we, pm=1.0, wu=0.0, wl=1.0, wa=0.0):   # wl: 적재 시간 가중(대기 비용에는 빈 차 이동만 들어가므로 1보다 작게), wa: 모든 출발지로 빈 차가 가는 시간 가중
    m = gp.Model(); m.Params.OutputFlag = 0
    z = m.addVars(M, vtype=GRB.BINARY, name="z")
    orig = [s for s in range(nd) if out_[s] > 0]
    x = m.addVars(orig, arcs, lb=0.0, name="x")
    e = m.addVars(arcs, lb=0.0, name="e")
    g = m.addVars(arcs, lb=0.0, name="g")   # 뿌리 → 모든 노드
    h = m.addVars(arcs, lb=0.0, name="h")   # 모든 노드 → 뿌리
    outA = {v: [a for a in arcs if tail[a] == v] for v in range(N)}
    inA = {v: [a for a in arcs if head[a] == v] for v in range(N)}
    for o in orig:
        b = [0.0] * N
        b[pd[o]] += out_[o]
        for oo, d, f in F:
            if oo == o: b[pd[d]] -= f
        for v in range(N):
            m.addConstr(gp.quicksum(x[o, k, s] for k, s in outA[v]) - gp.quicksum(x[o, k, s] for k, s in inA[v]) == b[v])
        for k in range(M):
            m.addConstr(x[o, k, 0] <= out_[o] * (1 - z[k])); m.addConstr(x[o, k, 1] <= out_[o] * z[k])
    be = [0.0] * N
    for s in range(nd): be[pd[s]] += in_[s] - out_[s]
    for v in range(N):
        m.addConstr(gp.quicksum(e[a] for a in outA[v]) - gp.quicksum(e[a] for a in inA[v]) == be[v])
        m.addConstr(gp.quicksum(g[a] for a in outA[v]) - gp.quicksum(g[a] for a in inA[v]) == (N - 1 if v == 0 else -1))
        m.addConstr(gp.quicksum(h[a] for a in outA[v]) - gp.quicksum(h[a] for a in inA[v]) == (-(N - 1) if v == 0 else 1))
    for k in range(M):
        m.addConstr(e[k, 0] <= lam * (1 - z[k])); m.addConstr(e[k, 1] <= lam * z[k])
        m.addConstr(g[k, 0] <= (N - 1) * (1 - z[k])); m.addConstr(g[k, 1] <= (N - 1) * z[k])
        m.addConstr(h[k, 0] <= (N - 1) * (1 - z[k])); m.addConstr(h[k, 1] <= (N - 1) * z[k])
    load = gp.quicksum(ln[k, s] * x[o, k, s] for o in orig for k, s in arcs)
    emp = gp.quicksum(ln[a] * e[a] for a in arcs)
    if variant == "load": obj = load + 1e-6 * emp
    elif variant == "both": obj = load + we * emp
    else:  # time(분/시간): 거리/속도 + 교차로 진입마다 p
        fac = 60.0 / P["at"]
        tj = lambda a: (pm * P["p"] if isJ[head[a]] else 0.0)
        obj = fac * (wl * gp.quicksum((ln[k, s] / P["vc"] + tj((k, s))) * x[o, k, s] for o in orig for k, s in arcs)
                     + gp.quicksum((ln[a] / P["ve"] + tj(a)) * e[a] for a in arcs))
        if wu > 0 or wa > 0:  # 출발지로 빈 차가 가는 시간: 각 스테이션에서(도착 주문 수 비율로) 그 출발지까지. 긴급 주문은 wu, 일반 주문은 wa
            ud = P["urgentDest"]; tot = sum(in_)
            wts = {}
            for o, d, f in F: wts[o] = wts.get(o, 0.0) + f * (wu if d == ud else wa)
            for uo in sorted(o for o in wts if wts[o] > 0):
                fu = wts[uo]
                u = m.addVars(arcs, lb=0.0)
                for v in range(N):
                    b = sum(in_[s2] for s2 in range(nd) if pd[s2] == v and s2 != uo) - ((tot - in_[uo]) if v == pd[uo] else 0.0)
                    m.addConstr(gp.quicksum(u[a] for a in outA[v]) - gp.quicksum(u[a] for a in inA[v]) == b)
                for k in range(M):
                    m.addConstr(u[k, 0] <= tot * (1 - z[k])); m.addConstr(u[k, 1] <= tot * z[k])
                obj = obj + fac * (fu / tot) * gp.quicksum((ln[a] / P["ve"] + tj(a)) * u[a] for a in arcs)
    m.setObjective(obj, GRB.MINIMIZE)
    return m, z

res, seen = [], set()
z0 = "".join(str(b) for b in P["z0"])
VARS = [("load", 0, 1, 0), ("both", 0.5, 1, 0), ("both", 1.0, 1, 0), ("time", 0, 1, 0)]
if len(sys.argv) > 5 and sys.argv[5] == "ext":   # 2차 후보: 교차로 가중(pm) · 긴급 출발지 가중(wu)
    VARS = [("time", 0, pm, wu) for pm in (1, 2, 3) for wu in (0, 3, 10, 30) if not (pm == 1 and wu == 0)]
if len(sys.argv) > 5 and sys.argv[5] == "ext2":  # 3차 후보: 2차에서 좋았던 근처를 더 촘촘히
    VARS = [("time", 0, pm, wu) for pm in (1.5, 2, 2.5, 3, 4) for wu in (0, 1.5, 5, 7, 10, 15, 20)]
if len(sys.argv) > 5 and sys.argv[5] == "ext3":  # 4차 후보(11/24 밤샘): 적재 가중 wl을 낮추고(대기는 빈 차 이동에서 나온다) 일반 출발지 가중 wa를 더한다
    VARS = [("time", 0, pm, wu, wl, wa) for wl in (0.3, 0.6) for pm in (2, 3) for wu, wa in ((10, 0), (20, 0), (5, 2), (10, 2))]
TL = float(sys.argv[6]) if len(sys.argv) > 6 else 40
for var in VARS:
    variant, we, pm, wu = var[:4]; wl, wa = (var[4], var[5]) if len(var) > 4 else (1.0, 0.0)
    m, z = build(variant, we, pm, wu, wl, wa)
    m.Params.PoolSearchMode = 2; m.Params.PoolSolutions = POOL; m.Params.PoolGap = GAP; m.Params.TimeLimit = TL
    m.optimize()
    tag = variant + (f"{we}" if variant == "both" else "") + (f"_p{pm}u{wu}" if (pm != 1 or wu) else "") + (f"_l{wl}a{wa}" if (wl != 1 or wa) else "")
    print(f"[{tag}] 상태 {m.Status}, 최적 {m.ObjVal:.2f}, 하한 {m.ObjBound:.2f}, 풀 {m.SolCount}개, {m.Runtime:.1f}초")
    for i in range(m.SolCount):
        m.Params.SolutionNumber = i
        bits = "".join("1" if z[k].Xn > 0.5 else "0" for k in range(M))
        if bits in seen: continue
        seen.add(bits)
        res.append({"dir": bits, "variant": tag, "rank": i, "obj": round(m.PoolObjVal, 4)})
# 후보마다 실제 최단거리로 Σf·d 와 「빈 차 최소 거리」(수송 LP)를 다시 계산해 붙인다
def empty_lb(D):
    m = gp.Model(); m.Params.OutputFlag = 0
    y = m.addVars(nd, nd, lb=0.0)
    for s in range(nd):
        m.addConstr(y.sum(s, "*") == in_[s]); m.addConstr(y.sum("*", s) == out_[s])
    m.setObjective(gp.quicksum(D[pd[a]][pd[b]] * y[a, b] for a in range(nd) for b in range(nd)), GRB.MINIMIZE)
    m.optimize(); return m.ObjVal
cands = [{"dir": z0, "variant": "z0", "rank": 0, "obj": None}] + res
for c in cands:
    D = floyd(c["dir"])
    c["ok"] = all(D[i][j] < float("inf") for i in range(N) for j in range(N))
    if not c["ok"]: continue
    c["sumFD"] = round(sum(f * D[pd[o]][pd[d]] for o, d, f in P["flows"]), 3)          # 웹 표시와 같은 단위(alpha 전)
    c["emptyLB"] = round(empty_lb(D) / P["alpha"], 3)                                   # 같은 단위
    c["chg"] = sum(1 for k in range(M) if c["dir"][k] != z0[k])
cands = [c for c in cands if c["ok"]]
json.dump({"case": arg, "edges": M, "z0": z0, "cands": cands}, open(outp, "w", encoding="utf-8"), indent=1)
best = sorted(cands, key=lambda c: c["sumFD"])[:5]
print(f"후보 {len(cands)}개(z0 포함) → {outp}")
print("z0:", cands[0]["sumFD"], "+ 빈 차", cands[0]["emptyLB"])
for c in best: print(f"  Σf·d {c['sumFD']} + 빈 차 {c['emptyLB']} | 바꾼 통로 {c['chg']} | {c['variant']} | {c['dir']}")
