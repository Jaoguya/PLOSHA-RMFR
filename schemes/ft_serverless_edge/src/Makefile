CXX = g++
CXXFLAGS = -O2 -std=c++17 -Wall
LDFLAGS = -lssl -lcrypto -lpthread
TARGET = ft_serverless_sim
SRCS = main.cpp ft_network.cpp ft_algorithms.cpp ft_experiments.cpp

$(TARGET): $(SRCS) ft_serverless_edge.hpp
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS) $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: clean
