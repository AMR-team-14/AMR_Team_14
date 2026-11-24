#pragma once

#include "case_data.hpp"
#include "policy.hpp"

struct SimMetrics {
    double cost = 999999.0;
    double w_bar = 999.0;
    double u_95 = 999.0;
    int max_q = 0;
    int end_q = 0;
};

SimMetrics run_sim(const SimData&, int stream_idx, int fleet, const Tree&, int q_cutoff = -1);
double eval_streams(const SimData&, int fleet, const Tree&, int start, int end, int q_cut = -1);
