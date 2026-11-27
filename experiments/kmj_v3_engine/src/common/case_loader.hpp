#pragma once
#include "types.hpp"

// ============================================================================
// 3. Case Loading & Corridor Decomposition
// ============================================================================
CaseData load_case(const std::string& path, const std::string& cname) {
    JsonVal j = load_json_file(path);
    CaseData cd;
    cd.name = cname;
    cd.title = j.str("title", cname);
    const auto& jnodes = j.at("nodes").arr;
    cd.n_nodes = (int)jnodes.size();
    cd.node_names.resize(cd.n_nodes);
    for (int i = 0; i < cd.n_nodes; ++i) cd.node_names[i] = jnodes[i].arr[0].sval;
    const auto& jedges = j.at("edges").arr;
    cd.n_edges = (int)jedges.size();
    cd.edges.resize(cd.n_edges);
    for (int e = 0; e < cd.n_edges; ++e) {
        cd.edges[e] = EdgeDef{(int)jedges[e].arr[0].nval, (int)jedges[e].arr[1].nval, jedges[e].arr[2].nval};
    }
    const auto& jpd = j.at("pd").arr;
    cd.n_stations = (int)jpd.size();
    cd.pd.resize(cd.n_stations);
    cd.node_to_st.assign(cd.n_nodes, -1);
    for (int s = 0; s < cd.n_stations; ++s) {
        cd.pd[s] = (int)jpd[s].nval;
        cd.node_to_st[cd.pd[s]] = s;
    }
    const auto& jdepts = j.at("depts").arr;
    cd.depts.resize(cd.n_stations);
    for (int s = 0; s < cd.n_stations; ++s) cd.depts[s] = jdepts[s].sval;
    const auto& jflows = j.at("flows").arr;
    cd.n_flows = (int)jflows.size();
    cd.flows.resize(cd.n_flows);
    cd.total_flow = 0.0;
    for (int f = 0; f < cd.n_flows; ++f) {
        cd.flows[f] = FlowDef{(int)jflows[f].arr[0].nval, (int)jflows[f].arr[1].nval, jflows[f].arr[2].nval};
        cd.total_flow += cd.flows[f].rate;
    }
    const auto& jz0 = j.at("z0").arr;
    cd.z0.resize(cd.n_edges, '0');
    for (int e = 0; e < cd.n_edges; ++e) cd.z0[e] = ((int)jz0[e].nval != 0) ? '1' : '0';
    cd.fixed_dir = j.str("fixedDir", "");
    const auto& jseeds = j.at("seeds").arr;
    for (const auto& sv : jseeds) cd.seeds.push_back((int)sv.nval);
    cd.urgent_dest = (int)j.num("urgentDest", 0);
    cd.alpha = j.num("alpha", 0.6);
    cd.vc = j.num("vc", 50.0);
    cd.ve = j.num("ve", 60.0);
    cd.tl = j.num("tl", 0.5);
    cd.tu = j.num("tu", 0.5);
    cd.at = j.num("at", 50.0);
    cd.p_occ = j.num("p", 0.3);
    cd.t_end = j.num("T", 3000.0);
    cd.warm = j.num("warm", 300.0);
    cd.qcap = (int)j.num("qcap", 300);
    cd.end_qmax = (int)j.num("endQmax", 30);
    cd.n_min = (int)j.num("nMin", 1);
    cd.n_max = (int)j.num("nMax", 30);
    cd.max_nodes = (int)j.num("maxNodes", 9);
    cd.c_agv = j.num("cAGV", 15.0);
    cd.c_w = j.num("cW", 0.6);
    cd.c_u = j.num("cU", 100.0);
    cd.scale = 60.0 / cd.at;
    cd.v_empty = cd.ve / cd.scale;
    cd.v_loaded = cd.vc / cd.scale;
    cd.t_load = cd.tl * cd.scale;
    cd.t_unload = cd.tu * cd.scale;
    cd.t_inter = cd.p_occ * cd.scale;
    cd.lam_hr = cd.total_flow * cd.alpha;
    if (j.has("meta")) {
        cd.has_meta = true;
        const auto& jm = j.at("meta");
        if (jm.has("sumFD_z0")) cd.meta_sum_fd_z0 = jm.num("sumFD_z0");
        if (jm.has("junctions")) cd.meta_junctions = (int)jm.num("junctions");
    }
    // Parallel lane detection: edges e1=(u,v) and e2=(v,u)
    cd.parallel_partner.assign(cd.n_edges, -1);
    cd.n_parallel_pairs = 0;
    for (int e1 = 0; e1 < cd.n_edges; ++e1) {
        if (cd.parallel_partner[e1] != -1) continue;
        for (int e2 = e1 + 1; e2 < cd.n_edges; ++e2) {
            if (cd.parallel_partner[e2] != -1) continue;
            if (cd.edges[e1].u == cd.edges[e2].v && cd.edges[e1].v == cd.edges[e2].u) {
                cd.parallel_partner[e1] = e2;
                cd.parallel_partner[e2] = e1;
                ++cd.n_parallel_pairs;
                break;
            }
        }
    }
    // Junctions: undirected degree >= 3 (using unique undirected neighbors)
    std::vector<std::set<int>> unbr(cd.n_nodes);
    std::vector<std::vector<int>> inc_edges(cd.n_nodes);
    for (int e = 0; e < cd.n_edges; ++e) {
        unbr[cd.edges[e].u].insert(cd.edges[e].v);
        unbr[cd.edges[e].v].insert(cd.edges[e].u);
        inc_edges[cd.edges[e].u].push_back(e);
        inc_edges[cd.edges[e].v].push_back(e);
    }
    cd.is_inter.assign(cd.n_nodes, 0);
    cd.n_junctions = 0;
    for (int v = 0; v < cd.n_nodes; ++v) {
        if ((int)unbr[v].size() >= 3) {
            cd.is_inter[v] = 1;
            ++cd.n_junctions;
        }
    }
    // Station flow balance
    cd.st_in.assign(cd.n_stations, 0.0);
    cd.st_out.assign(cd.n_stations, 0.0);
    cd.st_net.assign(cd.n_stations, 0.0);
    cd.st_urg_orig.assign(cd.n_stations, 0.0);
    for (const auto& fl : cd.flows) {
        cd.st_out[fl.o_st] += fl.rate;
        cd.st_in[fl.d_st] += fl.rate;
        if (fl.d_st == cd.urgent_dest) cd.st_urg_orig[fl.o_st] += fl.rate;
    }
    for (int s = 0; s < cd.n_stations; ++s) cd.st_net[s] = cd.st_in[s] - cd.st_out[s];
    // Build degree-2 corridors (atomic SCC-preserving flip units!)
    std::vector<uint8_t> used_edge(cd.n_edges, 0);
    // 1) Parallel pairs first (keep dir[e1] == dir[e2])
    for (int e = 0; e < cd.n_edges; ++e) {
        if (cd.parallel_partner[e] > e) {
            Corridor cor;
            cor.edges = {e, cd.parallel_partner[e]};
            cor.rel_sign = {0, 0};
            cor.is_parallel_pair = true;
            cd.corridors.push_back(cor);
            used_edge[e] = 1;
            used_edge[cd.parallel_partner[e]] = 1;
        }
    }
    // 2) Chains starting from junctions (nodes with inc_edges.size() != 2)
    for (int start = 0; start < cd.n_nodes; ++start) {
        if ((int)inc_edges[start].size() == 2) continue;
        for (int e0 : inc_edges[start]) {
            if (used_edge[e0]) continue;
            Corridor cor;
            int cur_v = start;
            int cur_e = e0;
            while (true) {
                used_edge[cur_e] = 1;
                int nxt_v = (cd.edges[cur_e].u == cur_v) ? cd.edges[cur_e].v : cd.edges[cur_e].u;
                int rsign = (cd.edges[cur_e].u == cur_v) ? 0 : 1;
                cor.edges.push_back(cur_e);
                cor.rel_sign.push_back(rsign);
                cur_v = nxt_v;
                if ((int)inc_edges[cur_v].size() != 2) break;
                int e_next = (inc_edges[cur_v][0] == cur_e) ? inc_edges[cur_v][1] : inc_edges[cur_v][0];
                if (used_edge[e_next]) break;
                cur_e = e_next;
            }
            cd.corridors.push_back(cor);
        }
    }
    // 3) Any pure cycle of degree-2 nodes
    for (int e0 = 0; e0 < cd.n_edges; ++e0) {
        if (used_edge[e0]) continue;
        Corridor cor;
        int cur_v = cd.edges[e0].u;
        int cur_e = e0;
        while (!used_edge[cur_e]) {
            used_edge[cur_e] = 1;
            int nxt_v = (cd.edges[cur_e].u == cur_v) ? cd.edges[cur_e].v : cd.edges[cur_e].u;
            int rsign = (cd.edges[cur_e].u == cur_v) ? 0 : 1;
            cor.edges.push_back(cur_e);
            cor.rel_sign.push_back(rsign);
            cur_v = nxt_v;
            int e_next = (inc_edges[cur_v][0] == cur_e) ? inc_edges[cur_v][1] : inc_edges[cur_v][0];
            cur_e = e_next;
        }
        cd.corridors.push_back(cor);
    }
    return cd;
}
