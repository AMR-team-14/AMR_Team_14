#pragma once

#include "simulator.hpp"

struct SeedScore {
    uint32_t seed;
    SimMetrics metrics;
};

std::vector<SeedScore> evaluate(SimData&, const Policy&, const std::vector<uint32_t>& seeds);
std::string evaluation_json(const Policy&, const std::vector<SeedScore>&);
std::string error_json(const std::string&);
bool rejected(const SeedScore&);
