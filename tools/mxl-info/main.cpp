#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <fmt/format.h>
#include <uuid.h>
#include <CLI/CLI.hpp>
#include <gsl/span>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>

namespace fs = std::filesystem;

std::ostream& operator<<(std::ostream& os, FlowInfo const& info)
{
    auto span = gsl::span<std::uint8_t, 16>(const_cast<std::uint8_t*>(info.id), sizeof(info.id));
    auto id = uuids::uuid(span);
    os << "- Flow [" << uuids::to_string(id) << ']' << '\n'
       << '\t' << fmt::format("{: >18}: {}", "Version", info.version) << '\n'
       << '\t' << fmt::format("{: >18}: {}", "Struct size", info.size) << '\n'
       << '\t' << fmt::format("{: >18}: {:0>8x}", "Flags", info.flags) << '\n'
       << '\t' << fmt::format("{: >18}: {}", "Head index", info.headIndex) << '\n'
       << '\t' << fmt::format("{: >18}: {}/{}", "Grain rate", info.grainRate.numerator, info.grainRate.denominator) << '\n'
       << '\t' << fmt::format("{: >18}: {}", "Grain count", info.grainCount) << '\n'
       << '\t' << fmt::format("{: >18}: {}", "Last write time", info.lastWriteTime) << '\n'
       << '\t' << fmt::format("{: >18}: {}", "Last read time", info.lastReadTime)
       << std::endl;

    auto const now = mxlGetTime();
    auto currentIndex = mxlTimestampToGrainIndex(&info.grainRate, now);
    os << '\t' << fmt::format("{: >18}: {}", "Latency (grains)", currentIndex - info.headIndex) << '\n'
       << '\t' << fmt::format("{: >18}: {}", "Latency (ns)", now - info.lastWriteTime)
       << std::endl;

    return os;
}

int listAllFlows(std::string const& in_domain)
{
    std::cout << "--- MXL Flows ---" << '\n';
    auto base = std::filesystem::path{in_domain};

    if (exists(base) && is_directory(base))
    {
        for (auto const& entry : std::filesystem::directory_iterator{base})
        {
            if (is_directory(entry) && (entry.path().extension() == ".mxl-flow"))
            {
                // this looks like a uuid. try to parse it an confirm it is valid.
                auto id = uuids::uuid::from_string(entry.path().stem().string());
                if (id.has_value())
                {
                    std::cout << "\t" << *id << '\n';
                }
            }
        }
    }

    std::cout << std::endl;
    return EXIT_SUCCESS;
}

int printFlow(std::string const& in_domain, std::string const& in_id)
{
    int ret = EXIT_SUCCESS;

    // Create the SDK instance with a specific domain.
    auto instance = mxlCreateInstance(in_domain.c_str(), "");
    if (instance == nullptr)
    {
        std::cerr << "Failed to create MXL instance" << std::endl;
        return EXIT_FAILURE;
    }

    // Create a flow reader for the given flow id.
    mxlFlowReader reader;
    mxlStatus status = mxlCreateFlowReader(instance, in_id.c_str(), "", &reader);
    if (status != MXL_STATUS_OK)
    {
        std::cerr << "Failed to create flow reader" << std::endl;
        mxlDestroyInstance(instance);
        return EXIT_FAILURE;
    }

    // Extract the FlowInfo structure.
    FlowInfo info;
    status = mxlFlowReaderGetInfo(instance, reader, &info);
    if (status != MXL_STATUS_OK)
    {
        std::cerr << "Failed to get flow info" << std::endl;
        ret = EXIT_FAILURE;
    }
    else
    {
        std::cout << info << std::endl;
        ret = EXIT_SUCCESS;
    }

    // Cleanup
    mxlDestroyFlowReader(instance, reader);
    mxlDestroyInstance(instance);

    return ret;
}

int main(int argc, char** argv)
{
    CLI::App app("mxl-info");

    mxlVersionType version;
    mxlGetVersion(&version);

    std::stringstream ss;
    ss << version.major << "." << version.minor << "." << version.bugfix << "." << version.build;

    // add version output
    app.set_version_flag("--version", ss.str());

    std::string domain = "";
    auto domainOpt = app.add_option("-d,--domain", domain, "The MXL domain directory");
    domainOpt->required(true);
    domainOpt->check(CLI::ExistingDirectory);

    std::string flowId = "";
    auto flowIdOpt = app.add_option("-f,--flow", flowId, "The flow id to analyse");

    auto allOpt = app.add_flag("-l,--list", "List all flows in the MXL domain");

    CLI11_PARSE(app, argc, argv);

    int status = EXIT_SUCCESS;

    if (allOpt->count() > 0)
    {
        status = listAllFlows(domain);
    }
    else if (flowIdOpt->count() > 0)
    {
        status = printFlow(domain, flowId);
    }

    return status;
}