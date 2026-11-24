#pragma once

#include "case_data.hpp"
#include <filesystem>

void run_analysis(const SimData&, const std::string& path, int threads);
void run_search(const SimData&, const std::filesystem::path& output, int threads);
