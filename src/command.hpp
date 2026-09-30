#pragma once
// Protocol-agnostic command, already validated. Produced by Dictionary,
// consumed by Dispatcher and Adapter. No knowledge of ZMQ or any wire format.
#include <cstdint>
#include <string>
#include <vector>

struct Command
{
    std::string target;              // e.g. "ares"
    std::string name;                // canonical command name, e.g. "set_state"
    std::uint16_t opcode = 0;        // device-local command enum value (NOT shared across devices)
    std::vector<std::int64_t> args;  // already type/range checked, in dictionary order
};