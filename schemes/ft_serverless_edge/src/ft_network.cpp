#include "ft_serverless_edge.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <queue>
#include <set>

// ── Helpers ──────────────────────────────────────────────────────────────
double FTServerlessEdgeSim::randDouble(double lo, double hi) {
    std::uniform_real_distribution<double> d(lo, hi);
    return d(rng_);
}
int FTServerlessEdgeSim::randInt(int lo, int hi) {
    std::uniform_int_distribution<int> d(lo, hi);
    return d(rng_);
}
double FTServerlessEdgeSim::linkDelay(int a, int b) const {
    if (a < 0 || b < 0 || a >= num_cloudlets_ || b >= num_cloudlets_) return INF_DELAY;
    return link_delays_[a][b];
}

// ── Dataset ──────────────────────────────────────────────────────────────
void FTServerlessEdgeSim::loadDataset() {
    std::ifstream f(cfg_.dataset_path);
    if (!f.is_open()) { std::cerr << "Cannot open dataset\n"; return; }
    std::string line;
    std::getline(f, line); // header
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        // Remove \r
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::stringstream ss(line);
        std::string tok;
        DatasetRow r;
        std::getline(ss, r.timestamp, ',');
        std::getline(ss, tok, ','); r.sensor_id = std::stoi(tok.substr(1)); // "S0"->0
        std::getline(ss, tok, ','); r.fog_node_id = std::stoi(tok.substr(1)); // "F0"->0
        std::getline(ss, tok, ','); r.temperature = std::stod(tok);
        std::getline(ss, tok, ','); r.pressure = std::stod(tok);
        std::getline(ss, tok, ','); r.vibration = std::stod(tok);
        std::getline(ss, tok, ','); r.is_failure = std::stoi(tok);
        std::getline(ss, tok, ','); // protocol skip
        std::getline(ss, tok, ','); r.connection_duration = std::stod(tok);
        std::getline(ss, tok, ','); r.flow_rate = std::stod(tok);
        std::getline(ss, tok, ','); r.bytes_transferred = std::stoi(tok);
        std::getline(ss, tok, ','); r.packet_size = std::stoi(tok);
        dataset_.push_back(r);
    }
}

// ── SEC Network Construction ─────────────────────────────────────────────
void FTServerlessEdgeSim::buildSECNetwork(int n) {
    num_cloudlets_ = n;
    cloudlets_.resize(n);
    link_delays_.assign(n, std::vector<double>(n, INF_DELAY));

    double r = std::max(1.0, cfg_.heterogeneity_ratio);
    double mean_cap = (MEM_CAPACITY_MIN_MB + MEM_CAPACITY_MAX_MB) / 2.0;

    for (int i = 0; i < n; i++) {
        cloudlets_[i].id = i;
        double scale = 1.0;
        if (r > 1.0 && n > 1) {
            double frac = static_cast<double>(i) / (n - 1);
            scale = (2.0 / (1.0 + r)) * (1.0 + frac * (r - 1.0));
        }
        cloudlets_[i].memory_capacity_mb = mean_cap * scale;
        cloudlets_[i].alpha = randDouble(ALPHA_MIN, ALPHA_MAX);
        cloudlets_[i].cold_start_delay_ms = randDouble(0.3, 1.5);
        cloudlets_[i].x = randDouble(0.0, 100.0);
        cloudlets_[i].y = randDouble(0.0, 100.0);
        cloudlets_[i].used_memory_mb = 0.0;
        link_delays_[i][i] = 0.0;
    }
    // Random geometric graph: connect cloudlets within threshold distance
    double threshold = 150.0 / std::sqrt((double)n / 10.0);
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            double dx = cloudlets_[i].x - cloudlets_[j].x;
            double dy = cloudlets_[i].y - cloudlets_[j].y;
            double dist = std::sqrt(dx * dx + dy * dy);
            if (dist < threshold) {
                double delay = TX_DELAY_MIN_MS + (dist / threshold) * (TX_DELAY_MAX_MS - TX_DELAY_MIN_MS);
                link_delays_[i][j] = delay;
                link_delays_[j][i] = delay;
            }
        }
    }
    // Ensure connectivity: connect each isolated node to nearest
    for (int i = 0; i < n; i++) {
        bool connected = false;
        for (int j = 0; j < n && !connected; j++)
            if (i != j && link_delays_[i][j] < INF_DELAY) connected = true;
        if (!connected) {
            double best = 1e18; int bj = (i + 1) % n;
            for (int j = 0; j < n; j++) {
                if (j == i) continue;
                double dx = cloudlets_[i].x - cloudlets_[j].x;
                double dy = cloudlets_[i].y - cloudlets_[j].y;
                double d = std::sqrt(dx*dx + dy*dy);
                if (d < best) { best = d; bj = j; }
            }
            double delay = randDouble(TX_DELAY_MIN_MS, TX_DELAY_MAX_MS);
            link_delays_[i][bj] = delay;
            link_delays_[bj][i] = delay;
        }
    }
}

// ── DAG Generation (deterministic per sensor_id) ─────────────────────────
void FTServerlessEdgeSim::generateDAG(DSPRequest& req) {
    std::mt19937 dag_rng(req.sensor_id * 31337 + 42);
    std::uniform_int_distribution<int> nf_dist(DAG_FUNC_MIN, DAG_FUNC_MAX);
    int nf = nf_dist(dag_rng);

    req.functions.resize(nf);
    req.dag_children.assign(nf, {});
    req.dag_parents.assign(nf, {});

    for (int i = 0; i < nf; i++) {
        req.functions[i].id = i;
        std::uniform_real_distribution<double> fp(FAIL_PROB_MIN, FAIL_PROB_MAX);
        req.functions[i].failure_prob = fp(dag_rng);
        req.functions[i].active_cloudlet = -1;
        req.functions[i].is_cold_start = true;
    }
    req.source_fn = 0;
    req.sink_fn = nf - 1;

    // Build random DAG edges (source→...→sink)
    for (int i = 1; i < nf; i++) {
        // Each node gets at least one parent from earlier nodes
        std::uniform_int_distribution<int> pd(0, i - 1);
        int parent = pd(dag_rng);
        req.dag_children[parent].push_back(i);
        req.dag_parents[i].push_back(parent);
        // Possibly add one more edge
        if (i > 1) {
            std::uniform_real_distribution<double> coin(0, 1);
            if (coin(dag_rng) < 0.3) {
                int p2 = pd(dag_rng);
                if (p2 != parent) {
                    req.dag_children[p2].push_back(i);
                    req.dag_parents[i].push_back(p2);
                }
            }
        }
    }
    // Ensure sink is reachable: if sink has no parents, add edge from nf-2
    if (req.dag_parents[nf - 1].empty() && nf > 1) {
        req.dag_children[nf - 2].push_back(nf - 1);
        req.dag_parents[nf - 1].push_back(nf - 2);
    }
}

// ── Topological Sort & Partition ─────────────────────────────────────────
void FTServerlessEdgeSim::topologicalSortAndPartition(DSPRequest& req) {
    int nf = (int)req.functions.size();
    // Compute in-degree based topological ordering
    std::vector<int> indeg(nf, 0);
    for (int i = 0; i < nf; i++)
        for (int c : req.dag_children[i]) indeg[c]++;
    std::queue<int> q;
    for (int i = 0; i < nf; i++) if (indeg[i] == 0) q.push(i);

    std::vector<int> topo;
    std::vector<int> level(nf, 0);
    while (!q.empty()) {
        int u = q.front(); q.pop();
        topo.push_back(u);
        for (int c : req.dag_children[u]) {
            level[c] = std::max(level[c], level[u] + 1);
            if (--indeg[c] == 0) q.push(c);
        }
    }
    // Group by level into partitions
    int max_level = *std::max_element(level.begin(), level.end());
    req.partitions.resize(max_level + 1);
    for (int i = 0; i < nf; i++)
        req.partitions[level[i]].push_back(i);
}

// ── Create a request from dataset row ────────────────────────────────────
DSPRequest FTServerlessEdgeSim::createRequest(int req_id, int sensor_id,
    int fog_id, double data_rate, int pkt_size, bool is_failure) {
    DSPRequest r;
    r.id = req_id;
    r.sensor_id = sensor_id;
    // Hotspot assignment: ~25% of sensors go to Node 0, the rest evenly to others
    if (sensor_id % 4 == 0) {
        r.origin_fog = 0;
    } else {
        r.origin_fog = 1 + (sensor_id % std::max(1, num_cloudlets_ - 1));
    }
    // Scale bytes_transferred to data rate in [0.01, 100] MB/s
    r.data_rate = DATA_RATE_MIN_MBS + (data_rate / 35000.0) * (DATA_RATE_MAX_MBS - DATA_RATE_MIN_MBS);
    r.data_rate = std::clamp(r.data_rate, DATA_RATE_MIN_MBS, DATA_RATE_MAX_MBS);
    r.fault_tol_req = randDouble(FT_REQ_MIN, FT_REQ_MAX);
    r.num_standby = 1;
    r.state_buffer_kb = STATE_BUFFER_BASE_KB + pkt_size * 0.01;
    r.predicted_rate = r.data_rate;
    r.processing_delay = 0; r.transmission_delay = 0;
    r.recovery_delay = 0; r.total_delay = 0;
    r.had_failure = is_failure;

    generateDAG(r);
    topologicalSortAndPartition(r);

    // Init EWMA values (one per timescale group, per Eq. 6)
    int gm = std::max(1, (int)(std::log2(std::max(1.0, r.data_rate)) - 1));
    r.ewma_values.assign(gm, r.data_rate);
    return r;
}
