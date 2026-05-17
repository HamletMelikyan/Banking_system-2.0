CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
LDFLAGS  = -lpthread

all: shm_init server client_tcp deinit

shm_init: shm_init.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

server: server.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

client_tcp: client_tcp.cpp colorprint.hpp
	$(CXX) $(CXXFLAGS) -o $@ client_tcp.cpp $(LDFLAGS)

deinit: deinit.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f shm_init server client_tcp deinit

.PHONY: all clean
