#pragma once
// Common interface every target adapter must implement.
// A target adapter owns both the codec (protocol encoding) and the
// transport (how bytes actually move) for exactly one target.
#include "command.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct SendResult
{
    bool ok = false;
    std::string message;                // human-readable status or error
    std::vector<std::uint8_t> wire;      // bytes that were (or would be) sent, for display/dry-run
    std::string topic;                   // frame 1 for topic-based transports; empty otherwise
};

class ITargetAdapter
{
public:
    virtual ~ITargetAdapter() = default;

    // Open the underlying transport (idempotent: safe to call more than once).
    virtual bool connect(std::string& error) = 0;

    // Encode only, no I/O. Used by both send() and dry-run mode.
    virtual bool encode(const Command& cmd, SendResult& out) = 0;

    // Encode and actually transmit.
    virtual SendResult send(const Command& cmd) = 0;

    // Release all resources. Must be safe to call multiple times.
    virtual void close() = 0;
};