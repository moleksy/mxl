#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <CLI/CLI.hpp>
#include <mxl/fabrics.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include <sys/types.h>
#include "../../lib/src/internal/FlowParser.hpp"
#include "../../lib/src/internal/Logging.hpp"

/*
    Example how to use:

        1- Start a receiver: ./mlx-fabrics-sample -f bda7a5e7-32c1-483a-ae1e-055568cc4335 --node 2.2.2.2 --service 1234 --provider verbs -cf low.json
        2- Paste the target info that gets printed in stdout to the -t argument of the sender.
        3- Start a sender: ./mlx-fabrics-sample -s -f bda7a5e7-32c1-483a-ae1e-055568cc4335 --node 1.1.1.1 --service 1234 --provider verbs -t
   <targetInfo>
*/

std::sig_atomic_t volatile g_exit_requested = 0;

struct Config
{
    // flow configuration
    std::string const& flowID;

    // endpoint configuration
    std::string const& node;
    std::string const& service;
    mxlFabricsProvider provider;
};

void signal_handler(int)

{
    g_exit_requested = 1;
}

static mxlStatus runSender(mxlInstance instance, Config const& config, mxlTargetInfo_t* targetInfo);
static mxlStatus runReceiver(mxlInstance instance, Config const& config, std::string const& flowConfigFile);

int main(int argc, char** argv)
{
    std::signal(SIGINT, &signal_handler);
    std::signal(SIGTERM, &signal_handler);

    CLI::App app("mxl-fabrics-sample");

    std::string flowConfigFile;
    auto _ = app.add_option(
        "-c, --flow-config-file", flowConfigFile, "The json file which contains the NMOS Flow configuration. Only used when configured as a reader.");

    std::string domain;
    auto domainOpt = app.add_option("-d,--domain", domain, "The MXL domain directory");
    domainOpt->required(true);
    domainOpt->check(CLI::ExistingDirectory);

    std::string flowID;
    auto flowIDOpt = app.add_option("-f, --flow-id",
        flowID,
        "The flow ID. When configured as a sender, this is the flow ID to read from. When configured as a receiver, this is the flow ID to write "
        "to.");
    flowIDOpt->required(true);

    bool runAsSender;
    auto runAsSenderOpt = app.add_flag("-s,--sender",
        runAsSender,
        "Run as a sender (flow reader + fabrics initiator). If not set, run as a receiver (fabrics target + flow writer).");
    runAsSenderOpt->default_val(true);

    std::string node;
    app.add_option("-n,--node",
        node,
        "This corresponds to the interface identifier of the fabrics endpoint, it can also be a logical address. This can be seen as the bind "
        "address when using sockets. "
        "Default is localhost.");

    std::string service;
    app.add_option("--service",
        service,
        "This corresponds to a service identifier for the fabrics endpoint. This can be seen as the bind port when using sockets. Default is 1234.");

    std::string provider;
    auto providerOpt = app.add_option("-p,--provider", provider, "The fabrics provider. One of (tcp, verbs or efa). Default is 'tcp'.");
    providerOpt->default_val("tcp");

    std::string targetInfo;
    app.add_option("-t,--target-info",
        targetInfo,
        "The target information. This is used when configured as a sender. This is the target information to send to. You first start the target and "
        "it will print the targetInfo that you paste to this argument");

    CLI11_PARSE(app, argc, argv);

    mxlFabricsProvider mxlProvider;
    auto status = mxlFabricsProviderFromString(provider.c_str(), &mxlProvider);
    if (status != MXL_STATUS_OK)
    {
        MXL_ERROR("Failed to parse provider '{}'", provider);
        return EXIT_FAILURE;
    }

    auto config = Config{
        .flowID = flowID,
        .node = node,
        .service = service,
        .provider = mxlProvider,
    };

    int exit_status = EXIT_SUCCESS;

    auto instance = mxlCreateInstance(domain.c_str(), "");
    if (instance == nullptr)
    {
        MXL_ERROR("Failed to create MXL instance");
        exit_status = EXIT_FAILURE;
        goto mxl_cleanup;
    }

    if (runAsSender)
    {
        mxlTargetInfo_t mxlTargetInfo;

        status = mxlFabricsTargetInfoFromString(targetInfo.c_str(), &mxlTargetInfo);
        if (status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to parse target info '{}'", targetInfo);
            exit_status = EXIT_FAILURE;
            goto mxl_cleanup;
        }

        exit_status = runSender(instance, config, &mxlTargetInfo);
    }
    else
    {
        exit_status = runReceiver(instance, config, flowConfigFile);
    }

mxl_cleanup:
    if (instance != nullptr)
    {
        mxlDestroyInstance(instance);
    }

    return exit_status;
}

static mxlStatus runSender(mxlInstance instance, Config const& config, mxlTargetInfo_t* targetInfo)
{
    mxlFlowReader reader;

    // Create a flow reader for the given flow id.
    mxlStatus status = mxlCreateFlowReader(instance, config.flowID.c_str(), "", &reader);
    if (status != MXL_STATUS_OK)
    {
        MXL_ERROR("Failed to create flow reader with status '{}'", static_cast<int>(status));
        return status;
    }

    mxlFabricsInitiator initiator;
    mxlInitiatorConfig initiatorConfig{
        .endpointAddress =
            {
                              .node = config.node.c_str(),
                              .service = config.service.c_str(),
                              },
        .memoryRegion =
            {
                              .address = nullptr,
                              .size = 0,
                              },
        .provider = config.provider,
    };

    status = mxlFabricsCreateInitiator(instance, &initiator);
    if (status != MXL_STATUS_OK)
    {
        MXL_ERROR("Failed to create fabrics initiator with status '{}'", static_cast<int>(status));
        return status;
    }

    status = mxlFabricsInitiatorSetup(instance, initiator, &initiatorConfig);
    if (status != MXL_STATUS_OK)
    {
        MXL_ERROR("Failed to setup fabrics initiator with status '{}'", static_cast<int>(status));
        return status;
    }

    status = mxlFabricsInitiatorAddTarget(instance, initiator, targetInfo);
    if (status != MXL_STATUS_OK)
    {
        MXL_ERROR("Failed to add target with status '{}'", static_cast<int>(status));
        return status;
    }

    // Extract the FlowInfo structure.
    FlowInfo flow_info;
    status = mxlFlowReaderGetInfo(instance, reader, &flow_info);
    if (status != MXL_STATUS_OK)
    {
        MXL_ERROR("Failed to get flow info with status '{}'", static_cast<int>(status));
        return status;
    }

    GrainInfo grainInfo;
    uint8_t* payload;

    uint64_t grainIndex = flow_info.headIndex + 1;

    while (g_exit_requested)
    {
        auto ret = mxlFlowReaderGetGrain(instance, reader, grainIndex, -1, &grainInfo, &payload); // TODO set a proper timeout...
        if (ret == MXL_ERR_OUT_OF_RANGE)
        {
            // I guess we are too late.. Reset to head index.
            grainIndex = flow_info.headIndex + 1;
        }
        if (ret != MXL_STATUS_OK)
        {
            // Missed a grain. resync.
            MXL_ERROR("Missed grain {}, err : {}", grainIndex, (int)ret);
            return status; // TODO better error code..
        }

        if (grainInfo.commitedSize != grainInfo.grainSize)
        {
            // we don't need partial grains. wait for the grain to be complete.
            continue;
        }

        ret = mxlFabricsInitiatorTransferGrain(instance, initiator, &grainInfo, payload);
        if (ret != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to transfer grain with status '{}'", static_cast<int>(ret));
            return status;
        }

        grainIndex++;
    }

    return MXL_STATUS_OK;
}

static mxlStatus runReceiver(mxlInstance instance, Config const& config, std::string const& flowConfigFile)
{
    std::ifstream file(flowConfigFile, std::ios::in | std::ios::binary);
    if (!file)
    {
        MXL_ERROR("Failed to open file: '{}'", flowConfigFile);
        return MXL_ERR_INVALID_ARG;
    }
    std::string flowDescriptor{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    mxl::lib::FlowParser descriptorParser{flowDescriptor};

    mxlFlowWriter writer;
    FlowInfo flowInfo;

    uint8_t* writerMemoryRegion;
    uint64_t writerMemoryRegionSize;

    auto status = mxlCreateFlow(instance, config.flowID.c_str(), flowDescriptor.c_str(), &flowInfo);
    if (status != MXL_STATUS_OK)
    {
        MXL_ERROR("Failed to create flow with status '{}'", static_cast<int>(status));
        return status;
    }

    status = mxlFlowGetMemoryRegion(instance, config.flowID.c_str(), &writerMemoryRegion, &writerMemoryRegionSize);
    if (status != MXL_STATUS_OK)
    {
        MXL_ERROR("Failed to get flow memory region with status '{}'", static_cast<int>(status));
        return status;
    }

    // Create a flow writer for the given flow id.
    status = mxlCreateFlowWriter(instance, config.flowID.c_str(), "", &writer);
    if (status != MXL_STATUS_OK)
    {
        MXL_ERROR("Failed to create flow writer with status '{}'", static_cast<int>(status));
        return status;
    }

    // create and setup the fabrics target
    mxlFabricsTarget target;
    mxlTargetInfo targetInfo;

    mxlTargetConfig targetConfig{
        .endpointAddress = {.node = config.node.c_str(), .service = config.service.c_str()},
        .memoryRegion =
            {
                            .address = writerMemoryRegion,
                            .size = writerMemoryRegionSize,
                            },
        .provider = config.provider,
    };

    status = mxlFabricsCreateTarget(instance, &target);
    if (status != MXL_STATUS_OK)
    {
        MXL_ERROR("Failed to create fabrics target with status '{}'", static_cast<int>(status));
        return status;
    }

    status = mxlFabricsTargetSetup(instance, target, &targetConfig, &targetInfo);
    if (status != MXL_STATUS_OK)
    {
        MXL_ERROR("Failed to setup fabrics target with status '{}'", static_cast<int>(status));
        return status;
    }

    auto targetInfoStr = std::string{};
    size_t targetInfoStrSize;

    status = mxlFabricsTargetInfoToString(&targetInfo, nullptr, &targetInfoStrSize);
    if (status != MXL_STATUS_OK)
    {
        MXL_ERROR("Failed to get target info string size with status '{}'", static_cast<int>(status));
        return status;
    }
    targetInfoStr.resize(targetInfoStrSize);

    status = mxlFabricsTargetInfoToString(&targetInfo, targetInfoStr.data(), &targetInfoStrSize);
    if (status != MXL_STATUS_OK)
    {
        MXL_ERROR("Failed to convert target info to string with status '{}'", static_cast<int>(status));
        return status;
    }

    MXL_INFO("Target info: {}", targetInfoStr);

    GrainInfo grainInfo;
    uint64_t grainIndex = 0;
    uint8_t* payload;

    //
    while (g_exit_requested)
    {
        status = mxlFabricsTargetWaitForNewGrain(instance, target, -1, &grainInfo, &payload, &grainIndex);
        if (status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to get grain with status '{}'", static_cast<int>(status));
            return status;
        }

        if (grainInfo.commitedSize != grainInfo.grainSize)
        {
            // This should never happen, I don't think it makes much sense to support partial grains for fabrics transfers.
            continue;
        }

        // We omit opening a grain, because the initiator writes directly to our memory region that we previously shared.
        status = mxlFlowWriterCommit(instance, writer, &grainInfo);
        if (status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to commit grain with status '{}'", static_cast<int>(status));
            return status;
        }
    }

    return MXL_STATUS_OK;
}
