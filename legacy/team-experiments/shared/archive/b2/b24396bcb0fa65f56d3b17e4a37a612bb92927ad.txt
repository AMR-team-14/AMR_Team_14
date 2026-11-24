// agvx — 웹 채점 엔진(agvSim_ · agvEvaluate_)을 연산 순서까지 그대로 옮긴 C++ 판.
// 난수(mulberry32 + 폰 노이만 지수분포) · 사건 순서 · 배차 키 비교(길이가 다른 키끼리의 비교 포함)가 웹과 같다.
// 검증: node cx_verify.js  (웹 원본 JS와 시드별 블록 비용이 소수점 끝까지 같은지 대조)
//
// 빌드: g++ -O2 -std=c++17 -ffp-contract=off -static -pthread agvx.cpp -o agvx.exe
// 사용: agvx case.txt            ← 표준입력으로 작업을 받는 서버 모드
//   J <id> <n> <seed0> <count> <block> <qcut> <id,f,dir,th,lo,hi;...>   작업 한 줄
//   GO                                                                   지금까지 받은 작업 실행
//   → R <id> <블록 비용...>   (반려 · 대기열 초과 블록은 inf),  끝에 DONE
//   D <n> <seed0> <count> <nodes>    진단(배차 상황별 대기 분해) 한 건을 바로 출력
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
using namespace std;

struct Case {
  int N = 0, nd = 0;
  vector<int> pd;
  vector<vector<double>> D;
  vector<vector<int>> NX;
  vector<int> Jn, jidx;
  struct Flow { int o, d; double rate; };
  vector<Flow> F;
  int urgentDest = 0, qcap = 0, endQmax = 0;
  double alpha = 0, vc = 0, ve = 0, tl = 0, tu = 0, at = 0, p = 0, T = 0, warm = 0, cAGV = 0, cW = 0, cU = 0;
  vector<string> depts;
} C;

struct TNode { int f = 0, dirMax = 0; double th = 0; int lo = 0, hi = 0; bool used = false; };
enum { F_URG, F_W, F_DE, F_DL, F_QO };
struct Design { int n = 0; vector<TNode> tree; };

struct Req { int o, d; double t; int id; };
enum { IDLE, DRV, WQ, CRS, LD, UL };
struct Veh {
  int id, node, st; double t1;
  Req req; int ph, to; double spd, total, pos, d1, wq0;
  int k, nl; int ln[16]; double la[16];
  double tdisp; int fresh, vloc; // 진단용
};

struct Diag {
  // [fresh 0/1][출발 부서] 건수, 대기 합, 배정 전 대기 합, 빈 차 이동거리 합
  double cnt[2][8] = {}, wsum[2][8] = {}, qsum[2][8] = {}, desum[2][8] = {};
  double pair[2][8][8] = {}; // [fresh][차 위치][출발 부서] 건수
  vector<array<double, 5>> urg; // 긴급: 대기, fresh, 차 위치, de, 배정 전 대기
};

struct Out { vector<double> ws, wu; int endQ = 0, maxQ = 0; bool unstable = false; };

static inline bool keyLess(const double* a, int al, const double* b, int bl) {
  for (int i = 0; i < al; i++) {
    if (i >= bl) continue; // JS: undefined와의 비교는 둘 다 false
    if (a[i] < b[i]) return true;
    if (a[i] > b[i]) return false;
  }
  return false;
}

void simulate(const Design& ds, uint32_t seed, Out& out, int qcut, Diag* dg) {
  const auto& D = C.D; const auto& NX = C.NX; const auto& PD = C.pd; const auto& F = C.F;
  const int nd = C.nd, nF = (int)F.size();
  double RATE = 0; for (int f = 0; f < nF; f++) RATE += F[f].rate;
  const double fac = 60 / C.at, mean = 60 / (C.alpha * RATE);
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
      double u = R(), prev = u; int n = 1;
      for (;;) { double v = R(); if (v < prev) { prev = v; n++; } else break; }
      if (n % 2 == 1) return k + u;
      k++;
    }
  };
  struct Jx { int busy = -1; vector<int> q; size_t head = 0; };
  vector<Jx> J(C.Jn.size());
  vector<Veh> V(ds.n);
  vector<Req> q; q.reserve(512);
  for (int i = 0; i < ds.n; i++) { V[i].id = i; V[i].node = i % nd; V[i].st = IDLE; V[i].t1 = 0; }
  double t = 0; int serial = 0;
  double next = EXP() * mean;

  auto startLeg = [&](Veh& v) {
    double d1 = v.k < v.nl ? v.la[v.k] : v.total;
    v.st = DRV; v.d1 = d1; v.t1 = t + (d1 - v.pos) / v.spd * fac;
  };
  auto travel = [&](Veh& v, int ph, int to, double spd) {
    int a = PD[v.node], b = PD[to], c = a; v.nl = 0;
    while (c != b) { c = NX[c][b]; if (c != b && C.jidx[c] >= 0) { v.ln[v.nl] = c; v.la[v.nl] = D[a][c]; v.nl++; } }
    v.ph = ph; v.to = to; v.spd = spd; v.total = D[a][b]; v.k = 0; v.pos = 0;
    startLeg(v);
  };
  auto handle = [&](Veh& v) {
    if (v.st == DRV) {
      v.pos = v.d1;
      if (v.k < v.nl) { v.st = WQ; v.wq0 = t; J[C.jidx[v.ln[v.k]]].q.push_back(v.id); return; }
      v.node = v.to;
      if (v.ph == 0) {
        const Req& r = v.req; double w = t - r.t;
        if (r.t >= C.warm) {
          out.ws.push_back(w); if (r.d == C.urgentDest) out.wu.push_back(w);
          if (dg) {
            int fr = v.fresh, o = r.o;
            dg->cnt[fr][o]++; dg->wsum[fr][o] += w; dg->qsum[fr][o] += v.tdisp - r.t; dg->desum[fr][o] += v.total;
            dg->pair[fr][v.vloc][o]++;
            if (r.d == C.urgentDest) dg->urg.push_back(array<double, 5>{w, (double)fr, (double)v.vloc, v.total, v.tdisp - r.t});
          }
        }
        v.st = LD; v.t1 = t + C.tl * fac;
      } else { v.st = UL; v.t1 = t + C.tu * fac; }
    } else if (v.st == CRS) {
      J[C.jidx[v.ln[v.k]]].busy = -1; v.k++; startLeg(v);
    } else if (v.st == LD) {
      travel(v, 1, v.req.d, C.vc);
    } else if (v.st == UL) {
      v.st = IDLE;
    }
  };
  auto grant = [&]() {
    for (size_t g = 0; g < J.size(); g++) {
      Jx& jj = J[g];
      if (jj.busy >= 0 || jj.head >= jj.q.size()) continue;
      Veh& v = V[jj.q[jj.head++]];
      if (jj.head == jj.q.size()) { jj.q.clear(); jj.head = 0; }
      v.st = CRS; v.t1 = t + C.p * fac; jj.busy = v.id;
    }
  };
  int idle[64], qo[16];
  double key[16], bk[16];
  auto dispatch = [&]() {
    for (;;) {
      int ni = 0;
      for (int m = 0; m < ds.n; m++) if (V[m].st == IDLE) idle[ni++] = m;
      if (!ni || q.empty()) return;
      for (int m = 0; m < nd; m++) qo[m] = 0;
      for (size_t m = 0; m < q.size(); m++) qo[q[m].o]++;
      int bv = -1, bb = -1, bl = 0;
      for (int a = 0; a < ni; a++) for (int b = 0; b < (int)q.size(); b++) {
        const Veh& v = V[idle[a]]; const Req& r = q[b];
        double de = D[PD[v.node]][PD[r.o]];
        int kl = 0, id = 1;
        for (int g = 0; g < 8 && id; g++) {
          const TNode& nn = ds.tree[id]; int side;
          if (nn.f == F_URG) side = r.d == C.urgentDest ? 1 : 0;
          else if (nn.f == F_W) side = (t - r.t) <= nn.th ? 0 : 1;
          else {
            double x = nn.f == F_DE ? de : nn.f == F_DL ? D[PD[r.o]][PD[r.d]] : (double)qo[r.o];
            side = x <= nn.th ? 0 : 1;
          }
          key[kl++] = nn.dirMax ? 1 - side : side;
          id = side ? nn.hi : nn.lo;
        }
        key[kl++] = de; key[kl++] = r.t; key[kl++] = r.id; key[kl++] = v.id;
        if (bv < 0 || keyLess(key, kl, bk, bl)) { memcpy(bk, key, sizeof(double) * kl); bl = kl; bv = idle[a]; bb = b; }
      }
      Veh& vv = V[bv]; Req rr = q[bb]; q.erase(q.begin() + bb);
      vv.req = rr; vv.tdisp = t; vv.fresh = (t - rr.t) <= 1e-9 ? 1 : 0; vv.vloc = vv.node;
      travel(vv, 0, rr.o, C.ve);
    }
  };
  auto due = [&](const Veh& v) { return v.st != IDLE && v.st != WQ && v.t1 <= t; };

  for (;;) {
    double tn = next;
    for (int m = 0; m < ds.n; m++) if (V[m].st != IDLE && V[m].st != WQ && V[m].t1 < tn) tn = V[m].t1;
    if (tn > C.T) break;
    t = tn;
    while (next <= t) {
      double u = R() * RATE; int k = 0;
      while (k < nF - 1 && u >= F[k].rate) { u -= F[k].rate; k++; }
      q.push_back({F[k].o, F[k].d, next, serial++});
      next = next + EXP() * mean;
    }
    for (;;) {
      for (int i = 0; i < ds.n; i++) while (due(V[i])) handle(V[i]);
      grant();
      dispatch();
      bool any = false;
      for (int i = 0; i < ds.n; i++) if (due(V[i])) { any = true; break; }
      if (!any) break;
    }
    if ((int)q.size() > out.maxQ) out.maxQ = (int)q.size();
    if ((int)q.size() > C.qcap || (qcut > 0 && (int)q.size() > qcut)) { out.unstable = true; out.endQ = (int)q.size(); return; }
  }
  out.endQ = (int)q.size();
  if (out.endQ > C.endQmax) out.unstable = true;
}

// 웹 agvEvaluate_와 같은 집계: 시드들의 대기시간을 한데 모아 평균과 긴급 95분위를 한 번 계산
double blockCost(const Design& ds, uint32_t seed0, int cnt, int qcut, double* meanOut = nullptr, double* u95Out = nullptr, int* maxQ = nullptr) {
  vector<double> ws, wu; int mq = 0;
  for (int s = 0; s < cnt; s++) {
    Out o; o.ws.reserve(9000); o.wu.reserve(2000);
    simulate(ds, seed0 + s, o, qcut, nullptr);
    if (o.unstable) return INFINITY;
    ws.insert(ws.end(), o.ws.begin(), o.ws.end()); wu.insert(wu.end(), o.wu.begin(), o.wu.end());
    if (o.maxQ > mq) mq = o.maxQ;
  }
  double sum = 0; for (double w : ws) sum += w;
  sort(wu.begin(), wu.end());
  double mean = ws.empty() ? 0 : sum / ws.size();
  double u95 = wu.empty() ? 0 : wu[(size_t)floor(0.95 * (wu.size() - 1))];
  double lam = 0; for (auto& f : C.F) lam += f.rate; lam = C.alpha * lam;
  if (meanOut) *meanOut = mean; if (u95Out) *u95Out = u95; if (maxQ) *maxQ = mq;
  return C.cAGV * ds.n + C.cW * lam * mean + C.cU * u95;
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
  }
  return ds.tree[1].used;
}

void loadCase(const char* path) {
  FILE* fp = fopen(path, "r"); if (!fp) { fprintf(stderr, "case 파일 없음: %s\n", path); exit(1); }
  auto rd = [&]() { double x; if (fscanf(fp, "%lf", &x) != 1) { fprintf(stderr, "case 읽기 실패\n"); exit(1); } return x; };
  C.N = (int)rd(); C.nd = (int)rd();
  C.pd.resize(C.nd); for (auto& x : C.pd) x = (int)rd();
  C.D.assign(C.N, vector<double>(C.N)); for (auto& r : C.D) for (auto& x : r) x = rd();
  C.NX.assign(C.N, vector<int>(C.N)); for (auto& r : C.NX) for (auto& x : r) x = (int)rd();
  int nj = (int)rd(); C.Jn.resize(nj); C.jidx.assign(C.N, -1);
  for (int i = 0; i < nj; i++) { C.Jn[i] = (int)rd(); C.jidx[C.Jn[i]] = i; }
  int nf = (int)rd(); C.F.resize(nf);
  for (auto& f : C.F) { f.o = (int)rd(); f.d = (int)rd(); f.rate = rd(); }
  C.urgentDest = (int)rd(); C.alpha = rd(); C.vc = rd(); C.ve = rd(); C.tl = rd(); C.tu = rd(); C.at = rd(); C.p = rd();
  C.T = rd(); C.warm = rd(); C.qcap = (int)rd(); C.endQmax = (int)rd(); C.cAGV = rd(); C.cW = rd(); C.cU = rd();
  char buf[64]; for (int i = 0; i < C.nd; i++) { if (fscanf(fp, "%63s", buf) == 1) C.depts.push_back(buf); }
  fclose(fp);
}

struct Job { string id; Design ds; uint32_t seed0; int count, block, qcut; vector<double> res; };

void diag(const Design& ds, uint32_t seed0, int cnt) {
  Diag dg; vector<double> ws, wu;
  for (int s = 0; s < cnt; s++) { Out o; simulate(ds, seed0 + s, o, 0, &dg); ws.insert(ws.end(), o.ws.begin(), o.ws.end()); }
  double tot = 0; for (int f = 0; f < 2; f++) for (int o = 0; o < C.nd; o++) tot += dg.cnt[f][o];
  printf("{\"n\":%d,\"total\":%.0f,\"depts\":[", ds.n, tot);
  for (int i = 0; i < C.nd; i++) printf("%s\"%s\"", i ? "," : "", C.depts[i].c_str());
  printf("],\"byOrigin\":[");
  bool first = true;
  for (int f = 0; f < 2; f++) for (int o = 0; o < C.nd; o++) {
    if (!dg.cnt[f][o]) continue;
    printf("%s{\"fresh\":%d,\"o\":%d,\"cnt\":%.0f,\"w\":%.4f,\"qw\":%.4f,\"de\":%.2f}", first ? "" : ",", f, o, dg.cnt[f][o],
           dg.wsum[f][o] / dg.cnt[f][o], dg.qsum[f][o] / dg.cnt[f][o], dg.desum[f][o] / dg.cnt[f][o]);
    first = false;
  }
  printf("],\"pair\":[");
  for (int f = 0; f < 2; f++) { printf("%s[", f ? "," : ""); for (int v = 0; v < C.nd; v++) { printf("%s[", v ? "," : ""); for (int o = 0; o < C.nd; o++) printf("%s%.0f", o ? "," : "", dg.pair[f][v][o]); printf("]"); } printf("]"); }
  // 긴급 상위 5% 꼬리 구성
  sort(dg.urg.begin(), dg.urg.end(), [](const array<double, 5>& a, const array<double, 5>& b) { return a[0] > b[0]; });
  size_t top = dg.urg.size() / 20; double tf = 0, tde = 0, tq = 0; map<int, int> byLoc;
  for (size_t i = 0; i < top; i++) { tf += dg.urg[i][1]; tde += dg.urg[i][3]; tq += dg.urg[i][4]; byLoc[(int)dg.urg[i][2]]++; }
  double uall = 0, ufr = 0; for (auto& u : dg.urg) { uall += u[0]; ufr += u[1]; }
  printf("],\"urg\":{\"cnt\":%zu,\"mean\":%.4f,\"freshShare\":%.4f,\"top5\":{\"cnt\":%zu,\"min\":%.4f,\"freshShare\":%.4f,\"de\":%.2f,\"qw\":%.4f,\"byLoc\":{",
         dg.urg.size(), uall / dg.urg.size(), ufr / dg.urg.size(), top, top ? dg.urg[top - 1][0] : 0, top ? tf / top : 0, top ? tde / top : 0, top ? tq / top : 0);
  bool f2 = true; for (auto& kv : byLoc) { printf("%s\"%s\":%d", f2 ? "" : ",", C.depts[kv.first].c_str(), kv.second); f2 = false; }
  printf("}}}}\n"); fflush(stdout);
}

int main(int argc, char** argv) {
  loadCase(argc > 1 ? argv[1] : "case.txt");
  int nth = argc > 2 ? atoi(argv[2]) : (int)thread::hardware_concurrency();
  if (nth < 1) nth = 1;
  vector<Job> jobs; string line;
  while (getline(cin, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.rfind("J ", 0) == 0) {
      stringstream ss(line); string tag, nodes; Job j; long long s0;
      ss >> tag >> j.id >> j.ds.n >> s0 >> j.count >> j.block >> j.qcut >> nodes;
      j.seed0 = (uint32_t)s0;
      if (!parseNodes(nodes, j.ds)) { printf("R %s err\n", j.id.c_str()); continue; }
      jobs.push_back(std::move(j));
    } else if (line.rfind("D ", 0) == 0) {
      stringstream ss(line); string tag, nodes; Design ds; long long s0; int cnt;
      ss >> tag >> ds.n >> s0 >> cnt >> nodes;
      if (parseNodes(nodes, ds)) diag(ds, (uint32_t)s0, cnt);
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
      jobs.clear();
    }
  }
  return 0;
}
