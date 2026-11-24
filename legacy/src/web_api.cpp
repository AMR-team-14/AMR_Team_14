#include "evaluation.hpp"

#include <optional>
#include <stdexcept>

namespace {
std::optional<SimData> model;
std::string response;
}

extern "C" const char* agv_initialize(const char* case_json, const char* seed11_json) {
    try {
        model = load_case(case_json, seed11_json);
        response = "{}";
    } catch (const std::exception& error) {
        response = error_json(error.what());
    }
    return response.c_str();
}

extern "C" const char* agv_evaluate(const char* csv) {
    try {
        if (!model) throw std::runtime_error("Case data must be loaded first");
        const auto policy = parse_policy(csv, *model);
        response = evaluation_json(policy, evaluate(*model, policy, model->seeds));
    } catch (const std::exception& error) {
        response = error_json(error.what());
    }
    return response.c_str();
}
