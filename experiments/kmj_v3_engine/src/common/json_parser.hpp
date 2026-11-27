#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#ifdef _OPENMP
#include <omp.h>
#endif

// ============================================================================
// 1. Minimal Recursive-Descent JSON DOM Parser (100% Schema-Safe, Zero Hardcode)
// ============================================================================
struct JsonVal {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool bval = false;
    double nval = 0.0;
    std::string sval;
    std::vector<JsonVal> arr;
    std::map<std::string, JsonVal> obj;
    bool has(const std::string& k) const { return type == Object && obj.count(k) > 0; }
    const JsonVal& at(const std::string& k) const {
        auto it = obj.find(k);
        if (it == obj.end()) {
            std::cerr << "JSON missing key: " << k << "\n";
            std::exit(1);
        }
        return it->second;
    }
    double num(const std::string& k, double def_v = 0.0) const {
        if (!has(k)) return def_v;
        return at(k).nval;
    }
    std::string str(const std::string& k, const std::string& def_v = "") const {
        if (!has(k)) return def_v;
        return at(k).sval;
    }
};
class JsonParser {
    const std::string& s;
    size_t i = 0;
    void skip_ws() {
        while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    }
    std::string parse_string() {
        skip_ws();
        if (i >= s.size() || s[i] != '"') { std::cerr << "JSON expected '\"' at " << i << "\n"; std::exit(1); }
        ++i;
        std::string out;
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) {
                ++i;
                if (s[i] == 'n') out.push_back('\n');
                else if (s[i] == 't') out.push_back('\t');
                else if (s[i] == 'r') out.push_back('\r');
                else out.push_back(s[i]);
                ++i;
            } else {
                out.push_back(s[i++]);
            }
        }
        if (i < s.size() && s[i] == '"') ++i;
        return out;
    }
    JsonVal parse_val() {
        skip_ws();
        if (i >= s.size()) { std::cerr << "JSON unexpected EOF\n"; std::exit(1); }
        char c = s[i];
        if (c == '{') {
            JsonVal v; v.type = JsonVal::Object;
            ++i; skip_ws();
            if (i < s.size() && s[i] == '}') { ++i; return v; }
            while (i < s.size()) {
                std::string k = parse_string();
                skip_ws();
                if (i < s.size() && s[i] == ':') ++i;
                v.obj[k] = parse_val();
                skip_ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; break; }
            }
            return v;
        }
        if (c == '[') {
            JsonVal v; v.type = JsonVal::Array;
            ++i; skip_ws();
            if (i < s.size() && s[i] == ']') { ++i; return v; }
            while (i < s.size()) {
                v.arr.push_back(parse_val());
                skip_ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; break; }
            }
            return v;
        }
        if (c == '"') {
            JsonVal v; v.type = JsonVal::String; v.sval = parse_string(); return v;
        }
        if (c == 't' && s.compare(i, 4, "true") == 0) {
            JsonVal v; v.type = JsonVal::Bool; v.bval = true; i += 4; return v;
        }
        if (c == 'f' && s.compare(i, 5, "false") == 0) {
            JsonVal v; v.type = JsonVal::Bool; v.bval = false; i += 5; return v;
        }
        if (c == 'n' && s.compare(i, 4, "null") == 0) {
            JsonVal v; v.type = JsonVal::Null; i += 4; return v;
        }
        // number
        size_t consumed = 0;
        double num = std::stod(s.substr(i), &consumed);
        i += consumed;
        JsonVal v; v.type = JsonVal::Number; v.nval = num; return v;
    }
public:
    explicit JsonParser(const std::string& src) : s(src) {}
    JsonVal parse() { return parse_val(); }
};
JsonVal load_json_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) { std::cerr << "Cannot open JSON file: " << path << "\n"; std::exit(1); }
    std::ostringstream ss; ss << in.rdbuf();
    std::string content = ss.str();
    JsonParser p(content);
    return p.parse();
}
