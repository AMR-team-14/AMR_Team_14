// agvg — 케이스 일반판 엔진. agvx(Case 1 전용)와 같은 시뮬레이션이지만 통로 방향(dir)을 작업마다 받는다.
// 웹 채점 코드(agvNet_ · agvSim_ · agvEvaluate_)의 연산 순서를 그대로 따른다: 플로이드-워셜의 k·i·j 순서와 '<' 갱신,
// 난수(mulberry32 + 폰 노이만 지수분포), 사건 순서, 배차 키 비교.
// 검증: node gx_verify.js <N>  (웹 원본 JS와 방향·대수·나무를 무작위로 바꿔 가며 묶음 비용을 대조)
//
// 빌드: g++ -O2 -std=c++17 -ffp-contract=off -static -pthread agvg.cpp -o agvg.exe
// 사용: agvg <case.txt> [스레드 수]      ← 표준입력으로 작업을 받는다. case.txt는 casetxt.js가 케이스 JSON에서 만든다.
//   J <id> <n> <seed0> <count> <block> <qcut> <dir 비트열> <id,f,dir,th,lo,hi;...>
//   GO   → R <id> <묶음 비용...> (반려 · 대기열 초과 · 강연결 아님은 inf), 끝에 DONE
//   E <n> <seed0> <count> <dir> <나무>   → 한 묶음의 평균 대기 · 긴급 95분위 · 비용 · 최대 대기열 · 적재 이동 합을 JSON 한 줄로
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
using namespace std;

struct Case {
  int N = 0, M = 0, nd = 0;
  vector<int> pd, ea, eb; vector<double> el;
  vector<int> Jn, jidx;
  struct Flow { int o, d; double rate; };
  vector<Flow> F;
  int urgentDest = 0, qcap = 0, endQmax = 0;
  double alpha = 0, vc = 0, ve = 0, tl = 0, tu = 0, at = 0, p = 0, T = 0, warm = 0, cAGV = 0, cW = 0, cU = 0;
} C;

// 방향 하나에 대한 최단거리 · 경로 표
struct Net {
  bool ok = false;
  vector<double> D;      // N×N
  vector<double> DD;     // nd×nd (부서 사이 거리)
  struct Route { int nl, off; double total; };
  vector<Route> R;       // nd×nd
  vector<int> jn; vector<double> la; // 경로가 지나는 교차로(번호)와 그때까지의 거리
};

shared_ptr<Net> buildNet(const string& bits) {
  auto net = make_shared<Net>();
  const int N = C.N; const double INF = INFINITY;
  if ((int)bits.size() != C.M) return net;
  vector<double> D(N * N, INF); vector<int> NX(N * N, -1);
  for (int i = 0; i < N; i++) { D[i * N + i] = 0; NX[i * N + i] = i; }
  for (int k = 0; k < C.M; k++) {
    int a = bits[k] == '1' ? C.eb[k] : C.ea[k], b = bits[k] == '1' ? C.ea[k] : C.eb[k];
    D[a * N + b] = C.el[k]; NX[a * N + b] = b;
  }
  for (int k = 0; k < N; k++) for (int i = 0; i < N; i++) {
    if (D[i * N + k] == INF) continue;
    for (int j = 0; j < N; j++) {
      double d = D[i * N + k] + D[k * N + j];
      if (d < D[i * N + j]) { D[i * N + j] = d; NX[i * N + j] = NX[i * N + k]; }
    }
  }
  for (int i = 0; i < N * N; i++) if (D[i] == INF) return net;
  net->ok = true; net->D = D;
  const int nd = C.nd; net->DD.resize(nd * nd); net->R.resize(nd * nd);
  for (int x = 0; x < nd; x++) for (int y = 0; y < nd; y++) {
    int a = C.pd[x], b = C.pd[y], c = a; Net::Route r; r.nl = 0; r.off = (int)net->jn.size();
    while (c != b) { c = NX[c * N + b]; if (c != b && C.jidx[c] >= 0) { net->jn.push_back(C.jidx[c]); net->la.push_back(D[a * N + c]); r.nl++; } }
    r.total = D[a * N + b]; net->R[x * nd + y] = r; net->DD[x * nd + y] = D[a * N + b];
  }
  return net;
}

struct TNode { int f = 0, dirMax = 0; double th = 0; int lo = 0, hi = 0; bool used = false; };
enum { F_URG, F_W, F_DE, F_DL, F_QO };
struct Design { int n = 0; vector<TNode> tree; shared_ptr<Net> net; };
struct Req { int o, d; double t; int id; };
enum { IDLE, DRV, WQ, CRS, LD, UL };
struct Out { vector<double> ws, wu; int endQ = 0, maxQ = 0; bool unstable = false; };

static inline bool keyLess(const double* a, int al, const double* b, int bl) {
  for (int i = 0; i < al; i++) {
    if (i >= bl) continue; // JS: undefined와의 비교는 둘 다 false
    if (a[i] < b[i]) return true;
    if (a[i] > b[i]) return false;
  }
  return false;
}

const int MAXV = 64;
void simulate(const Design& ds, uint32_t seed, Out& out, int qcut) {
  const Net& NT = *ds.net; const auto& F = C.F;
  const int nd = C.nd, nF = (int)F.size(), n = ds.n;
  const double* DD = NT.DD.data();
  double RATE = 0; for (int f = 0; f < nF; f++) RATE += F[f].rate;
  const double fac = 60 / C.at, mean = 60 / (C.alpha * RATE), INF = INFINITY;
  uint32_t rs = seed;
  auto R = [&]() -> double {
    rs += 0x6D2B79F5u;
    uint32_t t = (rs ^ (rs >> 15)) * (1u | rs);
    t = (t + (t ^ (t >> 7)) * (61u | t)) ^ t;
    return (double)(t ^ (t >> 14)) / 4294967296.0;
  };
  auto EXP = [&]() -> double {
    int k = 0;
    for (;;) {
      double u = R(), prev = u; int m = 1;
      for (;;) { double v = R(); if (v < prev) { prev = v; m++; } else break; }
      if (m % 2 == 1) return k + u;
      k++;
    }
  };
  struct Jx { int busy = -1; int q[MAXV]; unsigned head = 0, tail = 0; }; // 원형 대기열(동시에 기다리는 차 ≤ 대수 ≤ 64)
  const int nJ = (int)C.Jn.size();
  vector<Jx> J(nJ);
  struct V2 { int node, st, ph, to, k; const Net::Route* rt; double spd, pos, d1, wq0; Req req; };
  V2 V[MAXV]; double tact[MAXV];
  vector<Req> q; q.reserve(512);
  for (int i = 0; i < n; i++) { V[i].node = i % nd; V[i].st = IDLE; tact[i] = INF; }
  int nIdle = n, nWq = 0;
  double t = 0; int serial = 0;
  double next = EXP() * mean;

  auto startLeg = [&](int i) {
    V2& v = V[i];
    double d1 = v.k < v.rt->nl ? NT.la[v.rt->off + v.k] : v.rt->total;
    v.st = DRV; v.d1 = d1; tact[i] = t + (d1 - v.pos) / v.spd * fac;
  };
  auto travel = [&](int i, int ph, int to, double spd) {
    V2& v = V[i];
    v.rt = &NT.R[v.node * nd + to]; v.ph = ph; v.to = to; v.spd = spd; v.k = 0; v.pos = 0;
    startLeg(i);
  };
  auto handle = [&](int i) {
    V2& v = V[i];
    if (v.st == DRV) {
      v.pos = v.d1;
      if (v.k < v.rt->nl) { v.st = WQ; tact[i] = INF; v.wq0 = t; Jx& jj = J[NT.jn[v.rt->off + v.k]]; jj.q[jj.tail++ & (MAXV - 1)] = i; nWq++; return; }
      v.node = v.to;
      if (v.ph == 0) {
        const Req& r = v.req; double w = t - r.t;
        if (r.t >= C.warm) { out.ws.push_back(w); if (r.d == C.urgentDest) out.wu.push_back(w); }
        v.st = LD; tact[i] = t + C.tl * fac;
      } else { v.st = UL; tact[i] = t + C.tu * fac; }
    } else if (v.st == CRS) {
      J[NT.jn[v.rt->off + v.k]].busy = -1; v.k++; startLeg(i);
    } else if (v.st == LD) {
      travel(i, 1, v.req.d, C.vc);
    } else if (v.st == UL) {
      v.st = IDLE; tact[i] = INF; nIdle++;
    }
  };
  auto grant = [&]() {
    if (!nWq) return;
    for (int g = 0; g < nJ; g++) {
      Jx& jj = J[g];
      if (jj.busy >= 0 || jj.head == jj.tail) continue;
      int i = jj.q[jj.head++ & (MAXV - 1)];
      V[i].st = CRS; tact[i] = t + C.p * fac; jj.busy = i; nWq--;
    }
  };
  int idle[MAXV]; vector<int> qo(nd);
  double key[16], bk[16];
  auto dispatch = [&]() {
    while (nIdle && !q.empty()) {
      int ni = 0;
      for (int m = 0; m < n; m++) if (V[m].st == IDLE) idle[ni++] = m;
      for (int m = 0; m < nd; m++) qo[m] = 0;
      for (size_t m = 0; m < q.size(); m++) qo[q[m].o]++;
      int bv = -1, bb = -1, bl = 0;
      for (int a = 0; a < ni; a++) for (int b = 0; b < (int)q.size(); b++) {
        const V2& v = V[idle[a]]; const Req& r = q[b];
        double de = DD[v.node * nd + r.o];
        int kl = 0, id = 1;
        for (int g = 0; g < 8 && id; g++) {
          const TNode& nn = ds.tree[id]; int side;
          if (nn.f == F_URG) side = r.d == C.urgentDest ? 1 : 0;
          else if (nn.f == F_W) side = (t - r.t) <= nn.th ? 0 : 1;
          else {
            double x = nn.f == F_DE ? de : nn.f == F_DL ? DD[r.o * nd + r.d] : (double)qo[r.o];
            side = x <= nn.th ? 0 : 1;
          }
          key[kl++] = nn.dirMax ? 1 - side : side;
          id = side ? nn.hi : nn.lo;
        }
        key[kl++] = de; key[kl++] = r.t; key[kl++] = r.id; key[kl++] = idle[a];
        if (bv < 0 || keyLess(key, kl, bk, bl)) { memcpy(bk, key, sizeof(double) * kl); bl = kl; bv = idle[a]; bb = b; }
      }
      V2& vv = V[bv]; Req rr = q[bb]; q.erase(q.begin() + bb);
      vv.req = rr; nIdle--;
      travel(bv, 0, rr.o, C.ve);
    }
  };

  for (;;) {
    double tn = next;
    for (int m = 0; m < n; m++) if (tact[m] < tn) tn = tact[m];
    if (tn > C.T) break;
    t = tn;
    while (next <= t) {
      double u = R() * RATE; int k = 0;
      while (k < nF - 1 && u >= F[k].rate) { u -= F[k].rate; k++; }
      q.push_back({F[k].o, F[k].d, next, serial++});
      next = next + EXP() * mean;
    }
    for (;;) {
      for (int i = 0; i < n; i++) while (tact[i] <= t) handle(i);
      grant();
      dispatch();
      bool any = false;
      for (int i = 0; i < n; i++) if (tact[i] <= t) { any = true; break; }
      if (!any) break;
    }
    if ((int)q.size() > out.maxQ) out.maxQ = (int)q.size();
    if ((int)q.size() > C.qcap || (qcut > 0 && (int)q.size() > qcut)) { out.unstable = true; out.endQ = (int)q.size(); return; }
  }
  out.endQ = (int)q.size();
  if (out.endQ > C.endQmax) out.unstable = true;
}

// 웹 agvEvaluate_와 같은 집계: 시드들의 대기시간을 한데 모아 평균과 긴급 95분위를 한 번 계산
double blockCost(const Design& ds, uint32_t seed0, int cnt, int qcut, double* meanOut = nullptr, double* u95Out = nullptr, int* maxQ = nullptr, int* endQ = nullptr) {
  if (!ds.net || !ds.net->ok) return INFINITY;
  vector<double> ws, wu; int mq = 0, eq = 0;
  for (int s = 0; s < cnt; s++) {
    Out o; o.ws.reserve(9000); o.wu.reserve(2000);
    simulate(ds, seed0 + s, o, qcut);
    if (o.maxQ > mq) mq = o.maxQ; if (o.endQ > eq) eq = o.endQ;
    if (o.unstable) { if (maxQ) *maxQ = mq; if (endQ) *endQ = eq; return INFINITY; }
    ws.insert(ws.end(), o.ws.begin(), o.ws.end()); wu.insert(wu.end(), o.wu.begin(), o.wu.end());
  }
  double sum = 0; for (double w : ws) sum += w;
  sort(wu.begin(), wu.end());
  double mean = ws.empty() ? 0 : sum / ws.size();
  double u95 = wu.empty() ? 0 : wu[(size_t)floor(0.95 * (wu.size() - 1))];
  double lam = 0; for (auto& f : C.F) lam += f.rate; lam = C.alpha * lam;
  if (meanOut) *meanOut = mean; if (u95Out) *u95Out = u95; if (maxQ) *maxQ = mq; if (endQ) *endQ = eq;
  double cost = C.cAGV * ds.n + C.cW * lam * mean + C.cU * u95;
  // 보조 목적(탐색 전용, 제출 점수가 아니다): 긴급 대기가 TAILT분 이상인 비율에 TAILW를 곱해 더한다.
  // 95분위가 이동 시간 값의 계단에 올라앉으면 꼬리를 수십 건 줄일 때까지 점수가 안 변하므로, 탐색이 내려가는 쪽을 알아보게 한다.
  static const double TAILT = getenv("AGV_TAILT") ? atof(getenv("AGV_TAILT")) : 0, TAILW = getenv("AGV_TAILW") ? atof(getenv("AGV_TAILW")) : 0;
  if (TAILW > 0 && !wu.empty()) { size_t c = wu.end() - lower_bound(wu.begin(), wu.end(), TAILT); cost += TAILW * (double)c / wu.size(); }
  return cost;
}

bool parseNodes(const string& s, Design& ds) {
  ds.tree.assign(64, TNode());
  stringstream ss(s); string item;
  while (getline(ss, item, ';')) {
    if (item.empty()) continue;
    vector<string> f; stringstream is(item); string x;
    while (getline(is, x, ',')) f.push_back(x);
    if (f.size() != 6) return false;
    int id = atoi(f[0].c_str()); if (id < 1 || id >= 64) return false;
    TNode& n = ds.tree[id]; n.used = true;
    n.f = f[1] == "urg" ? F_URG : f[1] == "w" ? F_W : f[1] == "de" ? F_DE : f[1] == "dl" ? F_DL : F_QO;
    n.dirMax = f[2] == "max"; n.th = strtod(f[3].c_str(), nullptr); n.lo = atoi(f[4].c_str()); n.hi = atoi(f[5].c_str());
    if (n.lo < 0 || n.lo >= 64 || n.hi < 0 || n.hi >= 64) return false;
  }
  return ds.tree[1].used;
}

void loadCase(const char* path) {
  FILE* fp = fopen(path, "r"); if (!fp) { fprintf(stderr, "case 파일 없음: %s\n", path); exit(1); }
  auto rd = [&]() { double x; if (fscanf(fp, "%lf", &x) != 1) { fprintf(stderr, "case 읽기 실패\n"); exit(1); } return x; };
  C.N = (int)rd(); C.M = (int)rd(); C.nd = (int)rd(); int nf = (int)rd();
  C.pd.resize(C.nd); for (auto& x : C.pd) x = (int)rd();
  C.ea.resize(C.M); C.eb.resize(C.M); C.el.resize(C.M);
  vector<int> deg(C.N, 0);
  for (int k = 0; k < C.M; k++) { C.ea[k] = (int)rd(); C.eb[k] = (int)rd(); C.el[k] = rd(); deg[C.ea[k]]++; deg[C.eb[k]]++; }
  C.jidx.assign(C.N, -1);
  for (int i = 0; i < C.N; i++) if (deg[i] >= 3) { C.jidx[i] = (int)C.Jn.size(); C.Jn.push_back(i); }
  C.F.resize(nf);
  for (auto& f : C.F) { f.o = (int)rd(); f.d = (int)rd(); f.rate = rd(); }
  C.urgentDest = (int)rd(); C.alpha = rd(); C.vc = rd(); C.ve = rd(); C.tl = rd(); C.tu = rd(); C.at = rd(); C.p = rd();
  C.T = rd(); C.warm = rd(); C.qcap = (int)rd(); C.endQmax = (int)rd(); C.cAGV = rd(); C.cW = rd(); C.cU = rd();
  fclose(fp);
}

struct Job { string id; Design ds; uint32_t seed0; int count, block, qcut; vector<double> res; };

int main(int argc, char** argv) {
  loadCase(argc > 1 ? argv[1] : "case.txt");
  int nth = argc > 2 ? atoi(argv[2]) : (int)thread::hardware_concurrency();
  if (nth < 1) nth = 1;
  vector<Job> jobs; string line; map<string, shared_ptr<Net>> nets;
  auto getNet = [&](const string& bits) { auto it = nets.find(bits); if (it != nets.end()) return it->second; auto n = buildNet(bits); nets[bits] = n; return n; };
  while (getline(cin, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.rfind("J ", 0) == 0) {
      stringstream ss(line); string tag, bits, nodes; Job j; long long s0;
      ss >> tag >> j.id >> j.ds.n >> s0 >> j.count >> j.block >> j.qcut >> bits >> nodes;
      j.seed0 = (uint32_t)s0;
      if (j.ds.n < 1 || j.ds.n > MAXV || !parseNodes(nodes, j.ds)) { printf("R %s err\n", j.id.c_str()); continue; }
      j.ds.net = getNet(bits);
      jobs.push_back(std::move(j));
    } else if (line.rfind("E ", 0) == 0) {
      stringstream ss(line); string tag, bits, nodes; Design ds; long long s0; int cnt;
      ss >> tag >> ds.n >> s0 >> cnt >> bits >> nodes;
      if (ds.n < 1 || ds.n > MAXV || !parseNodes(nodes, ds)) { printf("{\"err\":\"parse\"}\n"); fflush(stdout); continue; }
      ds.net = buildNet(bits);
      if (!ds.net->ok) { printf("{\"ok\":false,\"err\":\"not strongly connected\"}\n"); fflush(stdout); continue; }
      double m = 0, u = 0; int mq = 0, eq = 0; double c = blockCost(ds, (uint32_t)s0, cnt, 0, &m, &u, &mq, &eq);
      double fd = 0; for (auto& f : C.F) fd += f.rate * ds.net->DD[f.o * C.nd + f.d];
      if (std::isinf(c)) printf("{\"ok\":true,\"stable\":false,\"maxQ\":%d,\"endQ\":%d,\"sumFD\":%.6f}\n", mq, eq, fd);
      else printf("{\"ok\":true,\"stable\":true,\"mean\":%.12f,\"u95\":%.12f,\"cost\":%.10f,\"maxQ\":%d,\"endQ\":%d,\"sumFD\":%.6f}\n", m, u, c, mq, eq, fd);
      fflush(stdout);
    } else if (line == "GO") {
      struct Task { int j, b; };
      vector<Task> tasks;
      for (size_t j = 0; j < jobs.size(); j++) {
        int nb = (jobs[j].count + jobs[j].block - 1) / jobs[j].block;
        jobs[j].res.assign(nb, 0);
        for (int b = 0; b < nb; b++) tasks.push_back({(int)j, b});
      }
      atomic<size_t> idx{0};
      auto work = [&]() {
        for (;;) {
          size_t i = idx.fetch_add(1); if (i >= tasks.size()) break;
          Job& J = jobs[tasks[i].j]; int b = tasks[i].b;
          int c = min(J.block, J.count - b * J.block);
          J.res[b] = blockCost(J.ds, J.seed0 + b * J.block, c, J.qcut);
        }
      };
      vector<thread> th; for (int i = 0; i < nth; i++) th.emplace_back(work);
      for (auto& x : th) x.join();
      for (auto& J : jobs) {
        printf("R %s", J.id.c_str());
        for (double v : J.res) { if (std::isinf(v)) printf(" inf"); else printf(" %.10f", v); }
        printf("\n");
      }
      printf("DONE\n"); fflush(stdout);
      jobs.clear(); nets.clear();
    }
  }
  return 0;
}
