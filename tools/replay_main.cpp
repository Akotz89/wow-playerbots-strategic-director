#include "pbguide/director.hpp"
#include "pbguide/replay.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: playerbots_strategic_replay_cli <replay.json>\n";
        return 2;
    }

    try
    {
        std::ifstream input(argv[1], std::ios::binary);
        if (!input)
            throw std::runtime_error("cannot open replay file");
        std::string const jsonText((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        pbguide::ReplayCase const replay = pbguide::ParseReplayCase(jsonText);
        pbguide::StrategicGuideDirector const director(replay.policy);
        pbguide::PlanDecision const decision =
            director.Select(replay.snapshot, replay.candidates, replay.failures);
        std::cout << pbguide::DecisionToJson(decision) << '\n';

        std::vector<std::string> const errors = pbguide::VerifyReplay(replay);
        for (std::string const& error : errors)
            std::cerr << error << '\n';
        return errors.empty() ? 0 : 1;
    }
    catch (std::exception const& error)
    {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
