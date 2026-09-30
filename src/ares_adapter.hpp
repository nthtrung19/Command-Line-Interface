#pragma once
// Ares adapter: ZeroMQ PUB, topic "MASTER_MESSAGE", payload = raw ExternalCmdPayload_t.
//
// This mirrors Ares's own ZmqChannel exactly (confirmed from ZmqChannel.cpp):
//   - a PUB socket BINDs (it does not connect) to config.topics[0].endpoint
//   - Send() writes two frames: the topic string, then the raw payload bytes
//   - the payload is a plain memcpy of a C struct, no serialization layer,
//     so byte order follows the host CPU (little-endian on x86_64), not
//     network byte order
//
// The CLI plays the "master" role on this bus: Ares devices are SUB sockets
// that CONNECT to whoever is bound here (see ZmqChannel.cpp, SUB/PUSH case).
#include "adapter.hpp"

#include <zmq.hpp>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

namespace ares
{
// Exact copy of Interface::ExternalCmdPayload_t from ZmqChannel.hpp.
// Keep this in sync with that header if it ever changes.
struct ExternalCmdPayload_t
{
    std::uint16_t deviceId;
    std::uint16_t command;
    std::uint8_t content[4];
};
static_assert(sizeof(ExternalCmdPayload_t) == 8, "Ares expects exactly 8 bytes on the wire");
}  // namespace ares

class AresAdapter : public ITargetAdapter
{
public:
    explicit AresAdapter(std::string endpoint = "tcp://127.0.0.1:5556",
                         std::string topic = "MASTER_MESSAGE")
        : endpoint_(std::move(endpoint)), topic_(std::move(topic))
    {
    }

    ~AresAdapter() override { close(); }

    bool connect(std::string& error) override
    {
        if (socket_) return true;   // already connected
        try
        {
            context_ = std::make_unique<zmq::context_t>(1);
            socket_ = std::make_unique<zmq::socket_t>(*context_, zmq::socket_type::pub);
            socket_->set(zmq::sockopt::linger, 0);   // never block on shutdown
            socket_->bind(endpoint_);

            // ZMQ PUB/SUB "slow joiner" problem: a message published before a
            // subscriber has finished connecting is silently dropped, with no
            // error anywhere. This short wait reduces (does not eliminate)
            // that risk for the very first command sent after startup.
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            return true;
        }
        catch (const zmq::error_t& e)
        {
            error = "cannot bind " + endpoint_ + ": " + e.what();
            close();
            return false;
        }
    }

    bool encode(const Command& cmd, SendResult& out) override
    {
        // Convention: the first argument is always device_id, the rest fill
        // content[] in order. This matches ExternalCmdPayload_t's fixed shape
        // and is sufficient for every command currently confirmed on Ares.
        // If a future device's payload does not fit this shape, this is the
        // one place that needs to change.
        if (cmd.args.empty())
        {
            out.message = "this command requires device_id as its first parameter";
            return false;
        }
        const std::size_t maxContentArgs = sizeof(ares::ExternalCmdPayload_t::content);
        if (cmd.args.size() > 1 + maxContentArgs)
        {
            out.message = "too many parameters for the 4-byte content field";
            return false;
        }

        ares::ExternalCmdPayload_t payload{};
        payload.deviceId = static_cast<std::uint16_t>(cmd.args[0]);
        payload.command = cmd.opcode;
        for (std::size_t i = 1; i < cmd.args.size(); ++i)
            payload.content[i - 1] = static_cast<std::uint8_t>(cmd.args[i]);

        out.wire.resize(sizeof(payload));
        std::memcpy(out.wire.data(), &payload, sizeof(payload));   // same raw copy Ares itself does
        out.topic = topic_;
        return true;
    }

    SendResult send(const Command& cmd) override
    {
        SendResult result;
        if (!encode(cmd, result)) return result;

        std::string error;
        if (!connect(error))
        {
            result.message = error;
            return result;
        }

        try
        {
            zmq::message_t topicFrame(result.topic.data(), result.topic.size());
            zmq::message_t payloadFrame(result.wire.data(), result.wire.size());
            socket_->send(topicFrame, zmq::send_flags::sndmore);
            socket_->send(payloadFrame, zmq::send_flags::none);
            result.ok = true;
            result.message = "published on " + endpoint_;
        }
        catch (const zmq::error_t& e)
        {
            result.message = std::string("send failed: ") + e.what();
        }
        return result;
    }

    void close() override
    {
        // Destroy the socket BEFORE the context. This is the reverse of a
        // confirmed bug in Ares's own device destructors (e.g. rwl_model.cpp),
        // which delete the context first while sockets built from it still
        // exist -- do not copy that ordering here.
        socket_.reset();
        context_.reset();
    }

private:
    std::string endpoint_;
    std::string topic_;
    std::unique_ptr<zmq::context_t> context_;
    std::unique_ptr<zmq::socket_t> socket_;
};