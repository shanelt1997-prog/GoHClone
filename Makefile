# Portable build for Linux / macOS / MinGW. Visual Studio users: open GoHClone.sln instead.
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
SRC      := $(wildcard src/*.cpp)
HDR      := $(wildcard src/*.h)
BIN      := build/GoHClone

$(BIN): $(SRC) $(HDR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC)

test: $(BIN)
	$(abspath $(BIN)) selftest

clean:
	rm -rf build

.PHONY: test clean
