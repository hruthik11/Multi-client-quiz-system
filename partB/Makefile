# Makefile for the Multi-Client Quiz System (Part B)

# Compiler and flags
CXX = g++
CXXFLAGS = -std=c++17 -Wall -pthread -I.

# Libraries to link (only needed for the server)
LIBS = -lcurl

# --- Define Executables ---
TARGETS = server_B client_B

# --- Build Rules ---

# The default rule, executed when you just type "make".
all: $(TARGETS)

# --- Compilation Rules ---

# Rule to compile the Part 1(B) server (with caching)
server_B: server_B.cpp
	$(CXX) $(CXXFLAGS) -o server_B server_B.cpp $(LIBS)

# Rule to compile the Part 1(B) client
client_B: client_B.cpp
	$(CXX) $(CXXFLAGS) -o client_B client_B.cpp

# --- Cleanup ---

# Rule to clean up all compiled files
clean:
	rm -f $(TARGETS)