// "Golden" files: precise fingerprints of a fixed set of battles.
//
//  * They lock the engine's behaviour, so an accidental change is caught immediately.
//  * They are the acceptance test for a port (e.g. to C# for Unity): the port must reproduce every
//    fingerprint exactly, including the hash of the complete event stream.
//
// Files live in <data folder>/../golden/ :
//     summary.txt            one fingerprint per battle + random-number test vectors
//     trace_*.txt            full event traces of two battles, for debugging a mismatch
#pragma once
#include "Battle.h"
#include "Defs.h"
#include "Rules.h"

#include <cstdint>
#include <map>
#include <string>

struct GoldenSet {
    std::string summary;
    std::map<std::string, std::string> traces; // file name -> contents
};

std::uint64_t fnv1a64(const std::string& bytes);

// Plays the golden matrix (every ordered pair of teams x seeds 1..3) and builds the files' contents.
GoldenSet buildGolden(const Content& content, const Rules& rules, const std::string& dataHash);

// Hash of rules.json + content.json, so golden files can tell when the data has changed.
std::string dataFingerprint(const std::string& dataDir);

// Result codes for verifyGolden.
enum class GoldenStatus { Match, Mismatch, NoFiles, DifferentData };

GoldenStatus verifyGolden(const std::string& dataDir, std::string& message);

// Command-line entry point: `golden` verifies, `golden --write` regenerates. Returns a process exit code.
int runGolden(const std::string& dataDir, bool write);
