// ares-cli: console (daniele77/cli) -> Dictionary -> Dispatcher -> target adapter.
//
// Only the "ares" target / "set_state" command is wired up right now, using
// values confirmed from Ares's real source (see dictionary.hpp for the
// exact citations). Adding a second command or target later means adding
// entries to Dictionary and, if needed, a new adapter -- nothing here
// changes.
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
#include <iomanip>
#include <iostream>
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
}  // namespace

int main(int argc, char** argv)
{
    std::string scriptPath;
    std::string endpoint = "tcp://127.0.0.1:5556";
    bool dryRun = false;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--script" && i + 1 < argc) scriptPath = argv[++i];
        else if (arg == "--endpoint" && i + 1 < argc) endpoint = argv[++i];
        else if (arg == "--dry-run") dryRun = true;
        else
        {
            std::cerr << "usage: ares_cli [--script FILE] [--endpoint EP] [--dry-run]\n";
            return 2;
        }
    }

    Dictionary dictionary;
    Dispatcher dispatcher;
    dispatcher.setDryRun(dryRun);
    dispatcher.registerAdapter("ares", std::make_unique<AresAdapter>(endpoint));

    int failureCount = 0;

    // Build one cli::Menu submenu per target, and one command per dictionary entry.
    auto root = std::make_unique<cli::Menu>("ares-cli");

    for (const auto& targetEntry : dictionary.targets())
    {
        auto submenu = std::make_unique<cli::Menu>(targetEntry.first);

        for (const auto& commandDef : targetEntry.second.commands)
        {
            std::vector<std::string> paramNames;
            for (const auto& param : commandDef.params) paramNames.push_back(param.name);

            const std::string targetName = targetEntry.first;
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