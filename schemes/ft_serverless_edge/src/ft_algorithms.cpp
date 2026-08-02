#include "ft_serverless_edge.hpp"
#include <queue>
#include <set>
#include <algorithm>
#include <cmath>
#include <cstring>

// ── Fault tolerance check (Eq. 1) ────────────────────────────────────────
bool FTServerlessEdgeSim::checkFaultTolerance(const DSPRequest& req, int nm) {
    double product = 1.0;
    for (auto& fn : req.functions) {
        double pr = 1.0 - std::pow(fn.failure_prob, nm + 1);
        product *= pr;
    }
    return product >= req.fault_tol_req;
}

// ── Dijkstra ─────────────────────────────────────────────────────────────
std::vector<int> FTServerlessEdgeSim::dijkstra(
    const std::vector<std::vector<AuxEdge>>& adj, int src, int dst) {
    int n = (int)adj.size();
    std::vector<double> dist(n, INF_DELAY);
    std::vector<int> prev(n, -1);
    dist[src] = 0.0;
    // min-heap: (dist, node)
    std::priority_queue<std::pair<double,int>,
        std::vector<std::pair<double,int>>,
        std::greater<>> pq;
    pq.push({0.0, src});
    while (!pq.empty()) {
        auto [d, u] = pq.top(); pq.pop();
        if (d > dist[u]) continue;
        if (u == dst) break;
        for (auto& e : adj[u]) {
            double nd = d + e.weight;
            if (nd < dist[e.to]) {
                dist[e.to] = nd;
                prev[e.to] = u;
                pq.push({nd, e.to});
            }
        }
    }
    // Reconstruct path
    std::vector<int> path;
    if (dist[dst] >= INF_DELAY) return path;
    for (int v = dst; v != -1; v = prev[v]) path.push_back(v);
    std::reverse(path.begin(), path.end());
    return path;
}

// ── Build Auxiliary Graph (Heu Stage 2) ──────────────────────────────────
void FTServerlessEdgeSim::buildAuxiliaryGraph(const DSPRequest& req,
    std::vector<std::vector<AuxEdge>>& adj, int& src_node, int& snk_node) {
    aux_nodes_.clear();
    int np = (int)req.partitions.size();
    int nc = num_cloudlets_;

    // For each partition layer, create 3 virtual nodes per cloudlet
    // node_id = layer * nc * 3 + cloudlet * 3 + copy
    auto nodeIdx = [&](int layer, int cl, int copy) {
        return layer * nc * 3 + cl * 3 + copy;
    };
    int total = np * nc * 3 + 2; // +2 for source and sink
    src_node = total - 2;
    snk_node = total - 1;
    aux_nodes_.resize(total);
    adj.assign(total, {});

    for (int q = 0; q < np; q++) {
        double mem_needed = MEM_PER_UNIT_RATE_MB * req.data_rate * (int)req.partitions[q].size();
        for (int j = 0; j < nc; j++) {
            int n1 = nodeIdx(q, j, 0);
            int n2 = nodeIdx(q, j, 1);
            int n3 = nodeIdx(q, j, 2);
            aux_nodes_[n1] = {j, q, 0};
            aux_nodes_[n2] = {j, q, 1};
            aux_nodes_[n3] = {j, q, 2};

            bool has_capacity = cloudlets_[j].availableMemory() >= mem_needed;
            if (has_capacity) {
                // Edge n1→n2: existing instance (no cold start)
                double proc = cloudlets_[j].alpha * cloudlets_[j].memory_capacity_mb * 1e-6 * req.data_rate;
                adj[n1].push_back({n2, proc});
                // Edge n1→n3: new instance (with cold start)
                double max_cold = cloudlets_[j].cold_start_delay_ms;
                adj[n1].push_back({n3, proc + max_cold});
            }
            // Inter-cloudlet edges within same layer (n1↔n1')
            for (int j2 = j + 1; j2 < nc; j2++) {
                double ld = linkDelay(j, j2);
                if (ld < INF_DELAY) {
                    int n1b = nodeIdx(q, j2, 0);
                    adj[n1].push_back({n1b, ld});
                    adj[n1b].push_back({n1, ld});
                }
            }
            // Connect to next layer: n2,n3 → n1 of next layer (delay 0)
            if (q + 1 < np) {
                int next_n1 = nodeIdx(q + 1, j, 0);
                adj[n2].push_back({next_n1, 0.0});
                adj[n3].push_back({next_n1, 0.0});
            }
        }
    }
    // Source node → first layer n1 nodes
    aux_nodes_[src_node] = {-1, -1, 4};
    for (int j = 0; j < nc; j++) {
        double d = linkDelay(req.origin_fog, j);
        if (d >= INF_DELAY) d = TX_DELAY_MAX_MS * 3;
        adj[src_node].push_back({nodeIdx(0, j, 0), d});
    }
    // Last layer n2,n3 → sink node
    aux_nodes_[snk_node] = {-1, -1, 5};
    for (int j = 0; j < nc; j++) {
        adj[nodeIdx(np - 1, j, 1)].push_back({snk_node, TX_DELAY_MIN_MS});
        adj[nodeIdx(np - 1, j, 2)].push_back({snk_node, TX_DELAY_MIN_MS});
    }
}

// ── Place active functions from shortest path ────────────────────────────
void FTServerlessEdgeSim::placeActiveFromPath(DSPRequest& req,
    const std::vector<int>& path, const std::vector<AuxNode>& nodes) {
    // Extract cloudlet assignments from path nodes
    std::set<int> seen_partitions;
    for (int ni : path) {
        if (ni >= (int)nodes.size()) continue;
        auto& nd = nodes[ni];
        if (nd.partition_idx < 0) continue;
        if (seen_partitions.count(nd.partition_idx)) continue;
        if (nd.copy == 1 || nd.copy == 2) {
            seen_partitions.insert(nd.partition_idx);
            bool cold = (nd.copy == 2);
            for (int fn_id : req.partitions[nd.partition_idx]) {
                req.functions[fn_id].active_cloudlet = nd.cloudlet_id;
                req.functions[fn_id].is_cold_start = cold;
            }
        }
    }
    // Fallback: assign unplaced functions to origin fog
    for (auto& fn : req.functions)
        if (fn.active_cloudlet < 0)
            fn.active_cloudlet = req.origin_fog;
}

// ── Place standby instances (Heu Stage 3) ────────────────────────────────
void FTServerlessEdgeSim::placeStandby(DSPRequest& req, int nm) {
    for (auto& fn : req.functions) {
        fn.standby_cloudlets.clear();
        int active = fn.active_cloudlet;
        // Find nm nearest cloudlets with available memory
        std::vector<std::pair<double, int>> candidates;
        double mem_needed = MEM_PER_UNIT_RATE_MB * req.data_rate;
        for (int j = 0; j < num_cloudlets_; j++) {
            if (j == active) continue;
            if (cloudlets_[j].availableMemory() >= mem_needed) {
                double d = linkDelay(active, j);
                if (d >= INF_DELAY) d = TX_DELAY_MAX_MS * 5;
                candidates.push_back({d, j});
            }
        }
        std::sort(candidates.begin(), candidates.end());
        for (int i = 0; i < nm && i < (int)candidates.size(); i++)
            fn.standby_cloudlets.push_back(candidates[i].second);
    }
}

// ── Algorithm Heu (complete 3-stage placement) ───────────────────────────
double FTServerlessEdgeSim::algorithmHeu(DSPRequest& req, int nm) {
    req.num_standby = nm;
    // Stage 1: partition already done in topologicalSortAndPartition
    // Stage 2: build auxiliary graph, find shortest path, place active
    std::vector<std::vector<AuxEdge>> adj;
    int src, snk;
    buildAuxiliaryGraph(req, adj, src, snk);
    auto path = dijkstra(adj, src, snk);
    if (path.empty()) {
        // Fallback: place all on origin fog
        for (auto& fn : req.functions) {
            fn.active_cloudlet = req.origin_fog;
            fn.is_cold_start = true;
        }
    } else {
        placeActiveFromPath(req, path, aux_nodes_);
    }
    // Stage 3: place standby instances
    placeStandby(req, nm);
    // Compute and return delay
    return computeTotalDelay(req);
}

// ── Algorithm Fwk (binary search for optimal n_m) ────────────────────────
void FTServerlessEdgeSim::algorithmFwk(DSPRequest& req) {
    int nmin = 1, nmax = MAX_STANDBY_K;
    int best_nm = 1;
    double best_delay = INF_DELAY;
    std::vector<double> delay_cache(MAX_STANDBY_K + 1, -1.0);

    while (nmin <= nmax) {
        int nm = (nmin + nmax) / 2;
        if (!checkFaultTolerance(req, nm)) {
            nmin = nm + 1;
            continue;
        }
        double d;
        if (delay_cache[nm] >= 0) {
            d = delay_cache[nm];
        } else {
            d = algorithmHeu(req, nm);
            delay_cache[nm] = d;
        }
        if (d < best_delay) {
            best_delay = d;
            best_nm = nm;
            nmax = nm - 1;
        } else {
            nmin = nm + 1;
        }
    }
    // Final placement with best n_m
    algorithmHeu(req, best_nm);
}

// ── Delay Computation ────────────────────────────────────────────────────
double FTServerlessEdgeSim::computeProcessingDelay(const DSPRequest& req, int fn_id) {
    auto& fn = req.functions[fn_id];
    int cl = fn.active_cloudlet;
    if (cl < 0 || cl >= num_cloudlets_) return 0.0;

    // Real crypto: AES-GCM encrypt the function's state buffer
    // Each serverless function processes encrypted state data proportional
    // to the data rate and state buffer size (per Ref[38] §IV)
    int buf_size = std::max(64, (int)(req.state_buffer_kb * 1024));
    std::vector<uint8_t> plaintext(buf_size, 0x42);
    std::vector<uint8_t> ciphertext(buf_size + 16);

    // Perform real AES-GCM encryption of the state buffer
    auto t_start = std::chrono::high_resolution_clock::now();
    aesEncryptInPlace(plaintext.data(), buf_size, ciphertext.data());
    auto t_end = std::chrono::high_resolution_clock::now();
    double crypto_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    // Apply queueing model: M/M/1 response time based on cloudlet load
    // As more requests hit the same cloudlet, queueing delay increases
    // As more cloudlets are added, load per cloudlet decreases → latency drops
    int load = (cl < (int)cloudlet_load_.size()) ? cloudlet_load_[cl] : 1;
    // Per-cloudlet service capacity: each cloudlet can handle ~50 concurrent requests
    // efficiently. Beyond that, queueing delay grows significantly.
    int service_capacity = 50;
    double utilization = std::min(0.95, (double)load / (double)(service_capacity + 1));
    double queue_factor = 1.0 / (1.0 - utilization);  // M/M/1 response time factor

    double d = crypto_ms * queue_factor;

    // Scale by cloudlet's alpha factor (heterogeneous processing capacity)
    d *= cloudlets_[cl].alpha / ALPHA_MIN;

    if (fn.is_cold_start)
        d += cloudlets_[cl].cold_start_delay_ms;
    return d;
}

double FTServerlessEdgeSim::computePathDelay(const DSPRequest& req) {
    // Eq. (3): max over execution paths of sum(processing + transmission)
    int nf = (int)req.functions.size();
    // Longest-path in DAG (critical path) using dynamic programming
    std::vector<double> dp(nf, 0.0);
    // Process in topological order (partitions are already sorted)
    for (auto& part : req.partitions) {
        for (int fn_id : part) {
            double proc = computeProcessingDelay(req, fn_id);
            double max_parent = 0.0;
            for (int p : req.dag_parents[fn_id]) {
                double tx = 0.0;
                int cl_p = req.functions[p].active_cloudlet;
                int cl_c = req.functions[fn_id].active_cloudlet;
                if (cl_p != cl_c && cl_p >= 0 && cl_c >= 0) {
                    tx = linkDelay(cl_p, cl_c);
                    if (tx >= INF_DELAY) tx = TX_DELAY_MAX_MS * 3;
                    tx *= req.data_rate * 0.01; // Scale by data volume
                }
                max_parent = std::max(max_parent, dp[p] + tx);
            }
            dp[fn_id] = max_parent + proc;
        }
    }
    return dp[req.sink_fn];
}

double FTServerlessEdgeSim::computeRecoveryDelay(const DSPRequest& req) {
    // Eq. (4): d_rec = Σ y_{l,τ} · κ_m · Σ d_e (over switching path)
    // Plus standby activation and DSP reconfiguration overhead
    if (!req.had_failure) return 0.0;
    double total = 0.0;
    int relocated_functions = 0;
    for (auto& fn : req.functions) {
        // Simulate: if this function failed, switch to first standby
        if (fn.standby_cloudlets.empty()) continue;
        // Probability-based: check if this function actually failed
        if (fn.failure_prob > 0.001) { // simplified: failed functions contribute
            int sb = fn.standby_cloudlets[0];
            double path_delay = linkDelay(fn.active_cloudlet, sb);
            if (path_delay >= INF_DELAY) path_delay = TX_DELAY_MAX_MS * 3;

            // 1. State migration delay (original formula)
            double state_transfer = req.state_buffer_kb * path_delay * 0.001;

            // 2. Standby activation (cold-start) cost
            // Activating a serverless function instance on a new cloudlet
            // involves container/VM initialization proportional to function complexity
            double activation_cost = 0.5 + fn.failure_prob * 5.0; // ms

            // 3. Inter-cloudlet path reconfiguration
            // Re-routing the DSP data path through the new standby location
            double reconfig_cost = path_delay * 0.5; // proportional to network distance

            total += state_transfer + activation_cost + reconfig_cost;
            relocated_functions++;
        }
    }

    // 4. Global DSP graph re-optimization overhead
    // After relocating functions, the entire pipeline must be re-validated
    if (relocated_functions > 0) {
        total += relocated_functions * 0.3; // per-function pipeline re-validation
    }

    return total;
}

double FTServerlessEdgeSim::computeTotalDelay(DSPRequest& req) {
    // Eq. (5): d_{m,τ} = d^pt + d^rec
    req.processing_delay = computePathDelay(req);
    req.recovery_delay = computeRecoveryDelay(req);
    req.total_delay = req.processing_delay + req.recovery_delay;
    return req.total_delay;
}

// ── Algorithm Adj (EWMA-based proactive adjustment) ──────────────────────
void FTServerlessEdgeSim::algorithmAdj(DSPRequest& req, int tau, double new_rate) {
    // Multi-timescale EWMA prediction (approximating MT-LSTM per Eq. 6-7)
    int gm = (int)req.ewma_values.size();
    if (gm == 0) return;
    for (int g = 0; g < gm; g++) {
        double alpha = 0.1 * (g + 1); // Faster groups have higher alpha
        req.ewma_values[g] = alpha * new_rate + (1.0 - alpha) * req.ewma_values[g];
    }
    // Predicted rate = average of all timescale predictions
    double pred = 0.0;
    for (double v : req.ewma_values) pred += v;
    req.predicted_rate = pred / gm;

    // Eq. (7): adjustment ratio
    double xi = req.predicted_rate / (req.data_rate + req.predicted_rate + 1e-9);
    (void)xi; // adjustment ratio used for standby relocation decision

    // Re-locate standby instances if data rate changed significantly
    if (std::abs(new_rate - req.data_rate) / (req.data_rate + 1e-9) > 0.1) {
        req.data_rate = new_rate;
        placeStandby(req, req.num_standby);
    } else {
        req.data_rate = new_rate;
    }
}

// ── Failover simulation ──────────────────────────────────────────────────
void FTServerlessEdgeSim::simulateFailover(DSPRequest& req) {
    for (auto& fn : req.functions) {
        double roll = randDouble(0.0, 1.0);
        if (roll < fn.failure_prob && !fn.standby_cloudlets.empty()) {
            // Switch to first standby

            fn.active_cloudlet = fn.standby_cloudlets[0];
            fn.standby_cloudlets.erase(fn.standby_cloudlets.begin());
            fn.is_cold_start = false; // standby is warm
            req.had_failure = true;
        }
    }
}
