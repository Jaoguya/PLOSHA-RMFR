# Makefile for Robust IIoT (Ref[24]) — PPDA Scheme
# Requires: OpenSSL (libssl, libcrypto) for Paillier + ECDSA

CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra
LDFLAGS = -lssl -lcrypto

# Shared crypto library source
CRYPTO_DIR = ../../../src/crypto
CRYPTO_SRCS = $(CRYPTO_DIR)/paillier.cpp $(CRYPTO_DIR)/modified_ecdsa.cpp

# Simulation engine
SIM_SRCS = robust_iiot_sim.cpp

# Include paths
INCLUDES = -I$(CRYPTO_DIR)

# Targets
TARGETS = exp1_sensor_scalability exp3_workload_intensity exp5_loss_exposure

.PHONY: all clean

all: $(TARGETS)

exp1_sensor_scalability: exp1_main.cpp $(SIM_SRCS) $(CRYPTO_SRCS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

exp3_workload_intensity: exp3_main.cpp $(SIM_SRCS) $(CRYPTO_SRCS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

exp5_loss_exposure: exp5_main.cpp $(SIM_SRCS) $(CRYPTO_SRCS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGETS)
