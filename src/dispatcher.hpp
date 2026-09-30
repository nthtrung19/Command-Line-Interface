#pragma once
// Picks the right adapter for a Command's target and routes it there.
// Owns no protocol knowledge itself -- that all lives in the adapters.
#include "adapter.hpp"

#include <map>
#include <memory>
#include <string>

class Dispatcher
{
public:
    void registerAdapter(const std::string& target, std::unique_ptr<ITargetAdapter> adapter)
    {
        adapters_[target] = std::move(adapter);
    }

    // Dry-run mode: encode only, never touch the network. Useful for
    // checking the exact bytes a command would produce before it is real.
    void setDryRun(bool value) { dryRun_ = value; }
    bool dryRun() const { return dryRun_; }

    SendResult dispatch(const Command& cmd)
    {
        SendResult result;
        const auto it = adapters_.find(cmd.target);
        if (it == adapters_.end())
        {
            result.message = "no adapter registered for target '" + cmd.target + "'";
            return result;
        }

        if (dryRun_)
        {
            result.ok = it->second->encode(cmd, result);
            if (result.ok) result.message = "dry-run: nothing sent";
            return result;
        }

        return it->second->send(cmd);
    }

private:
    std::map<std::string, std::unique_ptr<ITargetAdapter>> adapters_;
    bool dryRun_ = false;
};