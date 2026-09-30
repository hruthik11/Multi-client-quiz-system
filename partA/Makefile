
CXX = g++

CXXFLAGS = -std=c++17 -Wall -pthread -I.

LIBS = -lcurl


TARGETS = server client

all: $(TARGETS)

server: server.cpp llm_handler.hpp protocol.h json.hpp
	$(CXX) $(CXXFLAGS) -o server server.cpp $(LIBS)

client: client.cpp protocol.h json.hpp
	$(CXX) $(CXXFLAGS) -o client client.cpp


clean:
	rm -f $(TARGETS)