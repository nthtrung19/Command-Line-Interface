#pragma once
// Command dictionary: kept as C++ (not YAML) deliberately, for now.
// Reason: the command enum and payload shape are confirmed to differ per
// device type (RwlMasterCommand != CommonMasterCommand_t in ZmqChannel.hpp,
// despite SET_STATE_CMD happening to be 1 in both). Until the schema across
// all 8 device types is confirmed stable, a compiled dictionary catches
// mistakes at build time instead of at runtime.
//
// All values below for "reaction_wheel_0" are confirmed from real source:
//   - opcode 1            = RwlMasterCommand::SET_STATE_CMD   (rwl_model.cpp)
//   - deviceId 9           = mission_config.json, equipment_configs.reaction_wheel_0.deviceId
//   - mode range 0..4      = OperatingMode enum                (rwl_model.hpp)
//     MALFUNCTION=0, OFF=1, BOOTING=2, BOOTLOADER=3, APPLICATION=4
#include "command.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

struct ParamDef
{
    std::string name;
    std::int64_t min;
    std::int64_t max;
};

struct CommandDef
{
    std::string name;
    std::uint16_t opcode;
    std::string help;
    std::vector<ParamDef> params;   // first param is always device_id, by convention (see AresAdapter)
};

struct TargetDef
{
    std::string name;
    std::vector<CommandDef> commands;
};

class Dictionary
{
public:
    Dictionary()
    {
        targets_["ares"] = TargetDef{
            "ares",
            {
                CommandDef{
                    "set_state",
                    1,   // RwlMasterCommand::SET_STATE_CMD, confirmed in rwl_model.cpp
                    "Set ReactionWheel operating mode "
                    "(0=MALFUNCTION 1=OFF 2=BOOTING 3=BOOTLOADER 4=APPLICATION)",
                    {
                        {"device_id", 0, 65535},
                        {"mode", 0, 4},   // OperatingMode enum range, confirmed in rwl_model.hpp
                    },
                },
            },
        };
    }

    const std::map<std::string, TargetDef>& targets() const { return targets_; }

    // Validate raw text tokens against the dictionary and produce a Command.
    // Returns nullopt and fills 'error' on any failure.
    std::optional<Command> validate(const std::string& target, const std::string& cmd,
                                    const std::vector<std::string>& tokens,
                                    std::string& error) const
    {
        const auto t = targets_.find(target);
        if (t == targets_.end())
        {
            error = "unknown target: " + target;
            return std::nullopt;
        }

        const CommandDef* def = nullptr;
        for (const auto& c : t->second.commands)
            if (c.name == cmd) def = &c;
        if (!def)
        {
            error = "unknown command '" + cmd + "' for target '" + target + "'";
            return std::nullopt;
        }

        if (tokens.size() != def->params.size())
        {
            error = "expected " + std::to_string(def->params.size()) +
                    " parameter(s): " + usage(*def);
            return std::nullopt;
        }

        Command out;
        out.target = target;
        out.name = def->name;
        out.opcode = def->opcode;

        for (std::size_t i = 0; i < tokens.size(); ++i)
        {
            std::int64_t value = 0;
            if (!parseInt(tokens[i], value))
            {
                error = "parameter '" + def->params[i].name + "' is not a number: " + tokens[i];
                return std::nullopt;
            }
            if (value < def->params[i].min || value > def->params[i].max)
            {
                error = "parameter '" + def->params[i].name + "' out of range [" +
                        std::to_string(def->params[i].min) + ", " +
                        std::to_string(def->params[i].max) + "]";
                return std::nullopt;
            }
            out.args.push_back(value);
        }
        return out;
    }

    static std::string usage(const CommandDef& d)
    {
        std::ostringstream s;
        s << d.name;
        for (const auto& p : d.params) s << " <" << p.name << ">";
        return s.str();
    }

private:
    // Accepts decimal and 0x-prefixed hex. Avoids std::stringstream's
    // uint8_t-as-character and hex-parsing pitfalls.
    static bool parseInt(const std::string& s, std::int64_t& v)
    {
        try
        {
            std::size_t pos = 0;
            v = std::stoll(s, &pos, 0);   // base 0: decimal, 0x hex, 0-prefixed octal
            return pos == s.size();
        }
        catch (...) { return false; }
    }

    std::map<std::string, TargetDef> targets_;
};