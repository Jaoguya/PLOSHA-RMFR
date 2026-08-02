/*
 * ft_serverless_edge.hpp
 * 
 * Discrete-event simulation of Ref[38]:
 *   Xu et al., "Efficient and Fault Tolerant Data Stream Processing
 *   with Uncertain Data Rates in Serverless Edge Computing,"
 *   IEEE Transactions on Services Computing, Vol. 19, No. 1, Jan/Feb 2026.
 *
 * Models: SEC network, DAG-based function placement (Fwk/Heu),
 *         active-standby fault tolerance, EWMA-based proactive adjustment (Adj).
 */

#ifndef FT_SERVERLESS_EDGE_HPP
#define FT_SERVERLESS_EDGE_HPP

#include <vector>
#include <string>
#include <random>
#include <unordered_map>
#include <limits>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <openssl/evp.h>
#include <openssl/rand.h>

// ─── Constants from paper §VI-A ────────────────────────────────────────────

constexpr double MEM_CAPACITY_MIN_MB   = 131072.0;  // 128 GB
constexpr double MEM_CAPACITY_MAX_MB   = 524288.0;  // 512 GB
constexpr double DATA_RATE_MIN_MBS     = 0.01;      // MB/s
constexpr double DATA_RATE_MAX_MBS     = 100.0;     // MB/s
constexpr double MEM_PER_UNIT_RATE_MB  = 85.0;      // MB per unit data rate
constexpr double ALPHA_MIN             = 0.05;       // Memory alloc impact
constexpr double ALPHA_MAX             = 0.09;
constexpr double TX_DELAY_MIN_MS       = 0.1;        // Per-unit transmission
constexpr double TX_DELAY_MAX_MS       = 0.5;
constexpr double FAIL_PROB_MIN         = 0.001;
constexpr double FAIL_PROB_MAX         = 0.003;
constexpr double FT_REQ_MIN            = 0.991;      // Fault-tolerant req δ
constexpr double FT_REQ_MAX            = 0.999;
constexpr int    MAX_STANDBY_K         = 3;
constexpr double COLD_START_DELAY_MS   = 0.8;        // Typical cold-start
constexpr int    DAG_FUNC_MIN          = 3;           // Functions per DAG
constexpr int    DAG_FUNC_MAX          = 8;
constexpr double STATE_BUFFER_BASE_KB  = 2.0;        // Base state buffer

static constexpr double INF_DELAY = 1e18;

// ─── Data structures ───────────────────────────────────────────────────────

struct Cloudlet {
    int    id;
    double memory_capacity_mb;
    double alpha;                // Impact factor α_j
    double cold_start_delay_ms;
    double x, y;                 // 2D position for topology
    double used_memory_mb;       // Current utilisation

    double availableMemory() const { return memory_capacity_mb - used_memory_mb; }
};

struct SFunction {
    int    id;
    double failure_prob;
    int    active_cloudlet;                // -1 if unplaced
    std::vector<int> standby_cloudlets;
    bool   is_cold_start;
};

struct DSPRequest {
    int    id;
    int    sensor_id;
    int    origin_fog;
    double data_rate;            // ρ_{m,τ}
    double fault_tol_req;        // δ_m
    int    num_standby;          // n_m
    double state_buffer_kb;      // κ_m

    std::vector<SFunction>           functions;
    std::vector<std::vector<int>>    dag_children;  // Adjacency list
    std::vector<std::vector<int>>    dag_parents;
    int source_fn, sink_fn;

    // Partition sequence S_m  (each partition = list of function ids)
    std::vector<std::vector<int>>    partitions;

    // EWMA prediction state (multi-timescale)
    std::vector<double> ewma_values;   // One per timescale group
    double predicted_rate;

    // Metrics per time-slot
    double processing_delay;
    double transmission_delay;
    double recovery_delay;
    double total_delay;
    bool   had_failure;
};

struct DatasetRow {
    std::string timestamp;
    int sensor_id;
    int fog_node_id;
    double temperature, pressure, vibration;
    int is_failure;
    double connection_duration;
    double flow_rate;
    int bytes_transferred;
    int packet_size;
};

// ─── Simulation configuration ──────────────────────────────────────────────

struct SimConfig {
    std::string dataset_path;
    int    experiment;          // 1–6
    double variable_value;
    int    num_cloudlets;       // Default 10
    int    num_sensors;         // Default from dataset (100)
    double failure_rate;        // Override for exp4 (0.02–0.20)
    int    num_micro_slots;     // For exp5 / exp6
    double reporting_rate_mult; // Multiplier for exp3
    unsigned seed;
    double heterogeneity_ratio; // For heterogeneous node capacities
};

struct SimResult {
    double variable_value;
    double primary_metric;
    double secondary_1;
    double secondary_2;
};

// ─── Auxiliary-graph node for Heu ──────────────────────────────────────────

struct AuxNode {
    int cloudlet_id;
    int partition_idx;
    int copy;                   // 0=type-1, 1=type-2, 2=type-3, 3=dummy
                                // 4=source, 5=sink
};

struct AuxEdge {
    int    to;
    double weight;
};

// ─── DES Engine ────────────────────────────────────────────────────────────

class FTServerlessEdgeSim {
public:
    explicit FTServerlessEdgeSim(const SimConfig& cfg);
    SimResult run();

    // Public for batch runner access
    void algorithmFwk(DSPRequest& req);
    void simulateFailover(DSPRequest& req);
    double computeTotalDelay(DSPRequest& req);
    void algorithmAdj(DSPRequest& req, int tau, double new_rate);
    double randDouble(double lo, double hi);

private:
    SimConfig cfg_;
    std::mt19937 rng_;

    // Real crypto state (for fair wall-clock comparison)
    std::vector<uint8_t> aes_key_;
    bool crypto_initialized_ = false;
    void initCrypto();
    int aesEncryptInPlace(const uint8_t* plaintext, int len, uint8_t* out);

    // Per-cloudlet load tracking for queueing model
    std::vector<int> cloudlet_load_;

    // SEC network
    int num_cloudlets_;
    std::vector<Cloudlet> cloudlets_;
    std::vector<std::vector<double>> link_delays_;   // Adj-matrix (ms)

    // Dataset
    std::vector<DatasetRow> dataset_;

    // Requests (active in simulation)
    std::vector<DSPRequest> requests_;

    // ─ Dataset ─
    void loadDataset();

    // ─ Network ─
    void buildSECNetwork(int n);

    // ─ Request / DAG generation ─
    DSPRequest createRequest(int req_id, int sensor_id, int fog_id,
                             double data_rate, int pkt_size, bool is_failure);
    void generateDAG(DSPRequest& req);
    void topologicalSortAndPartition(DSPRequest& req);

    // ─ Algorithm Fwk (binary search for n_m) ─
    bool checkFaultTolerance(const DSPRequest& req, int nm);

    // ─ Algorithm Heu (3-stage placement) ─
    double algorithmHeu(DSPRequest& req, int nm);
    // Stage 1: already done by topologicalSortAndPartition
    // Stage 2: active placement via auxiliary graph
    void buildAuxiliaryGraph(const DSPRequest& req,
                             std::vector<std::vector<AuxEdge>>& adj,
                             int& src_node, int& snk_node);
    std::vector<int> dijkstra(const std::vector<std::vector<AuxEdge>>& adj,
                              int src, int dst);
    void placeActiveFromPath(DSPRequest& req, const std::vector<int>& path,
                             const std::vector<AuxNode>& nodes);
    // Stage 3: standby placement
    void placeStandby(DSPRequest& req, int nm);

    // ─ Delay computation (Eqs. 2–5) ─
    double computeProcessingDelay(const DSPRequest& req, int fn_id);
    double computePathDelay(const DSPRequest& req);
    double computeRecoveryDelay(const DSPRequest& req);

    // ─ Failover (declared public above) ─

    // ─ Experiment runners ─
    SimResult runExp1_SensorScalability();
    SimResult runExp2_FogScalability();
    SimResult runExp3_WorkloadIntensity();
    SimResult runExp4_FailureRate();
    SimResult runExp5_LossExposure();
    SimResult runExp6_RecoveryComm();
    SimResult runExp9_SchedulingEfficiency();

    // ─ Helpers ─
    int    randInt(int lo, int hi);
    double linkDelay(int from, int to) const;

    // Aux-graph bookkeeping (used during Heu)
    std::vector<AuxNode> aux_nodes_;
};

#endif // FT_SERVERLESS_EDGE_HPP
