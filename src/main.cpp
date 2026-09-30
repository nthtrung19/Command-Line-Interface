// ares-cli: console (daniele77/cli) -> Dictionary -> Dispatcher -> target adapter.
//
// main.cpp is intentionally generic: it knows nothing about "ares" or "fsw"
// specifically. It reads TargetDef entries from the Dictionary and builds
// each adapter through the factory registry below. Adding a new target means
// adding one TargetDef (dictionary.hpp) and one factory entry (this file's
// buildAdapterFactories()) -- no other code changes.
#include "ares_adapter.hpp"
#include "dictionary.hpp"
#include "dispatcher.hpp"

#include <cli/cli.h>
#include <cli/clifilesession.h>
#include <cli/clilocalsession.h>
#include <cli/loopscheduler.h>

#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{
std::string toHex(const std::vector<std::uint8_t>& bytes)
{
    std::ostringstream s;
    s << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < bytes.size(); ++i)
        s << (i ? " " : "") << std::setw(2) << static_cast<int>(bytes[i]);
    return s.str();
}

// One factory per adapter type. Each factory only needs to know the
// connection keys its own adapter requires -- it never sees any other
// target's configuration.
using AdapterFactory = std::function<std::unique_ptr<ITargetAdapter>(const ConnectionConfig&)>;

std::map<std::string, AdapterFactory> buildAdapterFactories()
{
    return {
        {"zmq",
         [](const ConnectionConfig& cfg) -> std::unique_ptr<ITargetAdapter>
         {
             return std::make_unique<AresAdapter>(cfg.at("endpoint"));
         }},
        // {"udp",
        //  [](const ConnectionConfig& cfg) -> std::unique_ptr<ITargetAdapter>
        //  {
        //      return std::make_unique<FswAdapter>(cfg.at("ip"), cfg.at("port"));
        //  }},
    };
}

// Parses "--set target.key=value" overrides, e.g. "--set ares.endpoint=tcp://...".
// Generic across every target; never hardcodes a target name.
bool applyOverride(std::map<std::string, TargetDef>& targets, const std::string& spec,
                   std::string& error)
{
    const auto dot = spec.find('.');
    const auto eq = spec.find('=');
    if (dot == std::string::npos || eq == std::string::npos || eq < dot)
    {
        error = "expected --set <target>.<key>=<value>, got: " + spec;
        return false;
    }
    const std::string target = spec.substr(0, dot);
    const std::string key = spec.substr(dot + 1, eq - dot - 1);
    const std::string value = spec.substr(eq + 1);

    const auto it = targets.find(target);
    if (it == targets.end())
    {
        error = "unknown target in --set: " + target;
        return false;
    }
    it->second.connection[key] = value;
    return true;
}
}  // namespace

int main(int argc, char** argv)
{
    std::string scriptPath;
    bool dryRun = false;
    std::vector<std::string> overrides;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--script" && i + 1 < argc) scriptPath = argv[++i];
        else if (arg == "--set" && i + 1 < argc) overrides.push_back(argv[++i]);
        else if (arg == "--dry-run") dryRun = true;
        else
        {
            std::cerr << "usage: ares_cli [--script FILE] [--set target.key=value]... [--dry-run]\n";
            return 2;
        }
    }

    Dictionary dictionary;
    auto targets = dictionary.targets();   // mutable copy: overrides apply here, not to Dictionary itself

    for (const auto& spec : overrides)
    {
        std::string error;
        if (!applyOverride(targets, spec, error))
        {
            std::cerr << "error: " << error << "\n";
            return 2;
        }
    }

    const auto factories = buildAdapterFactories();

    Dispatcher dispatcher;
    dispatcher.setDryRun(dryRun);

    int failureCount = 0;

    auto root = std::make_unique<cli::Menu>("ares-cli");

    for (const auto& targetEntry : targets)
    {
        const TargetDef& targetDef = targetEntry.second;

        const auto factoryIt = factories.find(targetDef.adapterType);
        if (factoryIt == factories.end())
        {
            std::cerr << "warning: no adapter factory for type '" << targetDef.adapterType
                      << "' (target '" << targetDef.name << "' skipped)\n";
            continue;
        }
        dispatcher.registerAdapter(targetDef.name, factoryIt->second(targetDef.connection));

        auto submenu = std::make_unique<cli::Menu>(targetDef.name);

        for (const auto& commandDef : targetDef.commands)
        {
            std::vector<std::string> paramNames;
            for (const auto& param : commandDef.params) paramNames.push_back(param.name);

            const std::string targetName = targetDef.name;
            const std::string commandName = commandDef.name;

            submenu->Insert(
                commandName,
                [&dictionary, &dispatcher, &failureCount, targetName, commandName](
                    std::ostream& out, const std::vector<std::string>& args)
                {
                    std::string error;
                    auto command = dictionary.validate(targetName, commandName, args, error);
                    if (!command)
                    {
                        out << "error: " << error << "\n";
                        ++failureCount;
                        return;
                    }

                    const SendResult result = dispatcher.dispatch(*command);
                    if (!result.ok)
                    {
                        out << "error: " << result.message << "\n";
                        ++failureCount;
                        return;
                    }

                    if (!result.topic.empty()) out << "topic  : " << result.topic << "\n";
                    out << "payload: " << toHex(result.wire) << "  (" << result.wire.size()
                        << " bytes)\n";
                    out << result.message << "\n";
                },
                commandDef.help,
                paramNames);
        }
        root->Insert(std::move(submenu));
    }

    root->Insert(
        "dry_run",
        [&dispatcher](std::ostream& out, int on)
        {
            dispatcher.setDryRun(on != 0);
            out << "dry-run " << (on ? "on" : "off") << "\n";
        },
        "Encode and show bytes without sending (0/1)", {"0|1"});

    cli::Cli cli(std::move(root));

    cli.WrongCommandHandler(
        [&failureCount](std::ostream& out, const std::string& cmd)
        {
            out << "unknown command or wrong parameters: " << cmd << "  (try 'help')\n";
            ++failureCount;
        });
    cli.StdExceptionHandler(
        [&failureCount](std::ostream& out, const std::string& cmd, const std::exception& e)
        {
            out << "internal error in '" << cmd << "': " << e.what() << "\n";
            ++failureCount;
        });

    const bool scripted = !scriptPath.empty() || !isatty(STDIN_FILENO);
    if (scripted)
    {
        std::ifstream file;
        if (!scriptPath.empty())
        {
            file.open(scriptPath);
            if (!file)
            {
                std::cerr << "cannot open script " << scriptPath << "\n";
                return 2;
            }
        }
        std::istream& in = scriptPath.empty() ? std::cin : file;
        cli::CliFileSession session(cli, in, std::cout);
        session.Start();
        return failureCount ? 1 : 0;
    }

    cli::LoopScheduler scheduler;
    cli::CliLocalSession session(cli, scheduler, std::cout, 200);
    session.ExitAction([&scheduler](auto& out) { out << "bye\n"; scheduler.Stop(); });
    scheduler.Run();
    return 0;
}