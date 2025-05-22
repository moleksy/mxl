#include <csignal>
#include <CLI/CLI.hpp>
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <spdlog/cfg/env.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "../../lib/src/internal/Logging.hpp"
#include "Player.hpp"

namespace fs = std::filesystem;

/// Flag to indicate if the application should exit
std::sig_atomic_t volatile gExitRequested = 0;

/// Signal handler to set the exit flag
void signalHandler(int in_signal)
{
    switch (in_signal)
    {
        case SIGINT:  MXL_INFO("Received SIGINT, exiting..."); break;
        case SIGTERM: MXL_INFO("Received SIGTERM, exiting..."); break;
        default:      MXL_INFO("Received signal {}, exiting...", in_signal); break;
    }
    gExitRequested = 1;
}

int main(int argc, char* argv[])
{
    // Set up signal handlers for graceful shutdown
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Initialize logging in the current app (initialisation in the mxl shared object is done in the createinstance function).
    auto console = spdlog::stdout_color_mt("console");
    spdlog::set_default_logger(console);
    spdlog::cfg::load_env_levels("MXL_LOG_LEVEL");

    //
    // Command line argument parsing
    //
    std::string inputFile, domain;
    int64_t inFrame = 0;
    int64_t outFrame = -1;
    bool loop = false;

    CLI::App cli{"mxl-gst-videoplayer"};
    auto domainOpt = cli.add_option("-d,--domain", domain, "MXL Domain")->required();
    domainOpt->required(true);

    auto inputOpt = cli.add_option("-i,--input", inputFile, "Input media file/url")->required();
    inputOpt->required(true);
    inputOpt->check(CLI::ExistingFile);

    cli.add_option("--in-frame", inFrame, "Loop start frame (default 0)");
    cli.add_option("--out-frame", outFrame, "Loop end frame (default -1 for none)");
    cli.add_flag("--loop", loop, "Enable looping between in-frame and out-frame");

    CLI11_PARSE(cli, argc, argv);

    //
    // Initialize GStreamer
    //
    gst_init(&argc, &argv);

    // Simple scope guard to ensure GStreamer is de-initialized.
    // Replace with std::scope_exit when widely available ( C++23 )
    auto onExit = std::unique_ptr<void, std::function<void(void*)>>(nullptr, [](void*) { gst_deinit(); });

    //
    // Create the Player and open the input uri
    //
    auto player = std::make_unique<mxl::tools::gst::Player>(domain);
    if (!player->open(inputFile, inFrame, outFrame, loop))
    {
        MXL_ERROR("Failed to open input file: {}", inputFile);
        return -1;
    }

    //
    // Start the player
    //
    if (!player->start())
    {
        MXL_ERROR("Failed to start the player");
        gst_deinit();
        return -1;
    }

    while (!gExitRequested && player->isRunning())
    {
        // Wait for the player to finish
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (player->isRunning())
    {
        player->stop();
    }

    // Release the player
    player.reset();

    // Release gstreamer resources.
    gst_deinit();

    return 0;
}
