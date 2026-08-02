# Makefile for FedDQN (Ref[22]) — Adaptive Task Scheduling Scheme
# Now includes OpenSSL for real AES-GCM crypto (fair wall-clock comparison)

CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra
LDFLAGS = -lssl -lcrypto -lpthread

# Simulation engine
SIM_SRCS = fed_dqn_sim.cpp

# Targets
TARGETS = exp1_sensor_scalability exp2_fog_scalability exp3_workload_intensity exp4_failure_rate exp9_scheduling_efficiency

.PHONY: all clean

all: $(TARGETS)

exp1_sensor_scalability: exp1_main.cpp $(SIM_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

exp2_fog_scalability: exp2_main.cpp $(SIM_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

exp3_workload_intensity: exp3_main.cpp $(SIM_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

exp4_failure_rate: exp4_main.cpp $(SIM_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

exp9_scheduling_efficiency: exp9_main.cpp $(SIM_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGETS)
