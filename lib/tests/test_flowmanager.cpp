#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <uuid.h>
#include <catch2/catch_test_macros.hpp>
#include "../src/internal/FlowManager.hpp"
#include "../src/internal/PathUtils.hpp"
#include "Utils.hpp"

using namespace mxl::lib;

/// Simple utility function to get the domain path for the tests.
/// This will return a path to /dev/shm/mxl_domain on Linux and a path in the user's
/// home directory ($HOME/mxl_domain) on macOS.
/// \return the path to the mxl domain directory.  The directory may not exist yet.
auto getDomainPath() -> std::filesystem::path
{
#ifdef __linux__
    return std::filesystem::path{"/dev/shm/mxl_domain"};
#elif __APPLE__
    auto home = std::getenv("HOME");
    if (home)
    {
        return std::filesystem::path{home} / "mxl_domain";
    }
    else
    {
        throw std::runtime_error{"Environment variable HOME is not set."};
    }
#else
#   error "Unsupported platform. This is only implemented for Linux and macOS."
#endif
}

TEST_CASE("Flow Manager : Create Manager", "[flow manager]")
{
    auto const domain = getDomainPath();
    // Remove that path if it exists.
    remove_all(domain);

    // This should throw since the folder should not exist.
    REQUIRE_THROWS(
        [&]()
        {
            std::make_shared<FlowManager>(domain);
        }());

    // Create the mxl domain path.
    REQUIRE(create_directory(domain));

    auto const manager = std::make_shared<FlowManager>(domain);
    REQUIRE(manager->listFlows().size() == 0);
}

TEST_CASE("Flow Manager : Create Video Flow Structure", "[flow manager]")
{
    auto const domain = getDomainPath();
    // Clean out the mxl domain path, if it exists.
    remove_all(domain);
    REQUIRE(create_directory(domain));

    auto const flowDef = mxl::tests::readFile("data/v210_flow.json");
    auto const flowId = *uuids::uuid::from_string("5fbec3b1-1b0f-417d-9059-8b94a47197ed");
    auto const grainRate = Rational{60000, 1001};

    auto manager = std::make_shared<FlowManager>(domain);
    auto flowData = manager->createDiscreteFlow(flowId, flowDef, MXL_DATA_FORMAT_VIDEO, 5, grainRate, 1024);

    REQUIRE(flowData != nullptr);
    REQUIRE(flowData->isValid());
    REQUIRE(flowData->grainCount() == 5);

    auto const flowDirectory = makeFlowDirectoryName(domain, uuids::to_string(flowId));
    REQUIRE(exists(flowDirectory));
    REQUIRE(is_directory(flowDirectory));

    // Check that the flow SHM storage exists
    auto const flowFile = makeFlowDataFilePath(flowDirectory);
    REQUIRE(exists(flowFile));
    REQUIRE(is_regular_file(flowFile));

    // Check that the flow access file for the SHM storage exists
    auto const flowAccessFile = makeFlowAccessFilePath(flowDirectory);
    REQUIRE(exists(flowAccessFile));
    REQUIRE(is_regular_file(flowAccessFile));

    // Check that the resource definition exists and is a regular file
    auto const resourceDefinitionFile = makeFlowDescriptorFilePath(flowDirectory);
    REQUIRE(exists(resourceDefinitionFile));
    REQUIRE(is_regular_file(resourceDefinitionFile));

    // Check that the resource definition contains a literal copy of the
    // definiton passed to the manager.
    REQUIRE(mxl::tests::readFile(resourceDefinitionFile) == flowDef);

    // Expect no channel data storage in this flow
    auto const channelDataFile = makeChannelDataFilePath(flowDirectory);
    REQUIRE(!exists(channelDataFile));

    // Count the grains.
    auto const grainDir = makeGrainDirectoryName(flowDirectory);
    REQUIRE(exists(grainDir));
    REQUIRE(is_directory(grainDir));

    auto grainCount = 0U;
    for (auto const& entry : std::filesystem::directory_iterator{grainDir})
    {
        if (is_regular_file(entry))
        {
            grainCount += 1U;
        }
    }
    REQUIRE(grainCount == 5U);

    // This should throw since the flow metadata will already exist.
    REQUIRE_THROWS(
        [&]()
        {
            manager->createDiscreteFlow(flowId, flowDef, MXL_DATA_FORMAT_VIDEO, 5, grainRate, 1024);
        }());

    // This should throw since the flow metadata will already exist.
    REQUIRE_THROWS(
        [&]()
        {
            auto const sampleRate = Rational{48000, 1};
            manager->createContinuousFlow(flowId, flowDef, MXL_DATA_FORMAT_AUDIO, sampleRate, 8, sizeof(float), 8192);
        }());

    REQUIRE(manager->listFlows().size() == 1);

    // Close the flow.  it should not be available for a get operation after being closed.
    flowData.reset();

    REQUIRE(manager->listFlows().size() == 1);

    // Delete the flow.
    manager->deleteFlow(flowId);

    REQUIRE(manager->listFlows().size() == 0);

    // Confirm that files on disk do not exist anymore
    REQUIRE(!exists(flowDirectory));
}

TEST_CASE("Flow Manager : Create Audio Flow Structure", "[flow manager]")
{
    auto const domain = getDomainPath();
    // Clean out the mxl domain path, if it exists.
    remove_all(domain);
    REQUIRE(create_directory(domain));

    auto const flowDef = mxl::tests::readFile("data/audio_flow.json");
    auto const flowId = *uuids::uuid::from_string("b3bb5be7-9fe9-4324-a5bb-4c70e1084449");
    auto const flowString = to_string(flowId);
    auto const sampleRate = Rational{48000, 1};

    auto manager = std::make_shared<FlowManager>(domain);
    auto flowData = manager->createContinuousFlow(flowId, flowDef, MXL_DATA_FORMAT_AUDIO, sampleRate, 2, sizeof(float), 4096);

    REQUIRE(flowData != nullptr);
    REQUIRE(flowData->isValid());
    REQUIRE(flowData->channelCount() == 2U);
    REQUIRE(flowData->sampleWordSize() == sizeof(float));
    REQUIRE(flowData->channelBufferLength() == 4096U);
    REQUIRE(flowData->channelDataLength() == (flowData->channelCount() * flowData->channelBufferLength()));
    REQUIRE(flowData->channelDataSize() == (flowData->channelDataLength() * flowData->sampleWordSize()));

    auto const flowDirectory = makeFlowDirectoryName(domain, uuids::to_string(flowId));
    REQUIRE(exists(flowDirectory));
    REQUIRE(is_directory(flowDirectory));

    // Check that the flow SHM storage exists
    auto const flowFile = makeFlowDataFilePath(flowDirectory);
    REQUIRE(exists(flowFile));
    REQUIRE(is_regular_file(flowFile));

    // Check that the resource definition exists and is a regular file
    auto const resourceDefinitionFile = makeFlowDescriptorFilePath(flowDirectory);
    REQUIRE(exists(resourceDefinitionFile));
    REQUIRE(is_regular_file(resourceDefinitionFile));

    // Check that the resource definition contains a literal copy of the
    // definiton passed to the manager.
    REQUIRE(mxl::tests::readFile(resourceDefinitionFile) == flowDef);

    // Check that the channel data SHM storage exists
    auto const channelDataFile = makeChannelDataFilePath(flowDirectory);
    REQUIRE(exists(channelDataFile));
    REQUIRE(is_regular_file(channelDataFile));

    // Expect no grains in this flow
    auto const grainDir = makeGrainDirectoryName(flowDirectory);
    REQUIRE(!exists(grainDir));

    // This should throw since the flow metadata will already exist.
    REQUIRE_THROWS(
        [&]()
        {
            manager->createContinuousFlow(flowId, flowDef, MXL_DATA_FORMAT_AUDIO, sampleRate, 8, sizeof(float), 8192);
        }());

    // This should throw since the flow metadata will already exist.
    REQUIRE_THROWS(
        [&]()
        {
            auto const grainRate = Rational{60000, 1001};
            manager->createDiscreteFlow(flowId, flowDef, MXL_DATA_FORMAT_VIDEO, 5, grainRate, 1024);
        }());

    REQUIRE(manager->listFlows().size() == 1);

    // Close the flow. It should not be available for a get operation after being closed.
    flowData.reset();

    REQUIRE(manager->listFlows().size() == 1);

    // Delete the flow.
    manager->deleteFlow(flowId);

    REQUIRE(manager->listFlows().size() == 0);

    // Confirm that files on disk do not exist anymore
    REQUIRE(!exists(flowDirectory));
}

TEST_CASE("Flow Manager : Open, List, and Error Conditions", "[flow manager]") {
    auto const domain = getDomainPath();
    // start clean
    std::error_code ec;
    std::filesystem::remove_all(domain, ec);
    REQUIRE(std::filesystem::create_directory(domain));

    auto manager = std::make_shared<FlowManager>(domain);

    //
    // 1) Create & open a discrete flow
    //
    auto const flowId1 = *uuids::uuid::from_string("11111111-1111-1111-1111-111111111111");
    auto const flowDef1 = mxl::tests::readFile("data/v210_flow.json");
    auto const grainRate = Rational{60000, 1001};
    {
        auto flowData1 = manager->createDiscreteFlow(flowId1, flowDef1, MXL_DATA_FORMAT_VIDEO, 3, grainRate, 512);
        REQUIRE(flowData1->grainCount() == 3U);
        // close writer
        flowData1.reset();
    }
    // open in read-only mode
    {
        auto openData1 = manager->openFlow(flowId1, AccessMode::OPEN_READ_ONLY);
        REQUIRE(openData1);
        auto *d = dynamic_cast<DiscreteFlowData*>(openData1.get());
        REQUIRE(d);
        REQUIRE(d->grainCount() == 3U);
    }

    //
    // 2) Create & open a continuous flow
    //
    auto const flowId2 = *uuids::uuid::from_string("22222222-2222-2222-2222-222222222222");
    auto const flowDef2 = mxl::tests::readFile("data/audio_flow.json");
    auto const sampleRate = Rational{48000, 1};
    {
        auto flowData2 = manager->createContinuousFlow(flowId2, flowDef2, MXL_DATA_FORMAT_AUDIO, sampleRate, 4, sizeof(float), 2048);
        REQUIRE(flowData2->channelCount() == 4U);
        flowData2.reset();
    }
    {
        auto openData2 = manager->openFlow(flowId2, AccessMode::OPEN_READ_WRITE);
        REQUIRE(openData2);
        auto *c = dynamic_cast<ContinuousFlowData*>(openData2.get());
        REQUIRE(c);
        REQUIRE(c->channelCount() == 4U);
    }

    //
    // 3) listFlows should report both flows
    //
    {
        auto flows = manager->listFlows();
        REQUIRE(flows.size() == 2);
    }

    //
   // 4) deleteFlow(nullptr) returns false
    //
    {
        std::unique_ptr<FlowData> empty;
        REQUIRE(manager->deleteFlow(std::move(empty)) == false);
   }

    //
    // 5) delete by ID and verify removal
    //
    REQUIRE(manager->deleteFlow(flowId1));
    REQUIRE(manager->listFlows().size() == 1);
    REQUIRE(manager->deleteFlow(flowId2));
    REQUIRE(manager->listFlows().empty());

    //
    // 6) openFlow invalid mode should throw
    //
    REQUIRE_THROWS_AS(
        manager->openFlow(flowId1, AccessMode::CREATE_READ_WRITE),
        std::invalid_argument
    );

    //
    // 7) opening a non-existent flow throws filesystem_error
    //
    auto const flowId3 = *uuids::uuid::from_string("33333333-3333-3333-3333-333333333333");
    REQUIRE_THROWS_AS(
        manager->openFlow(flowId3, AccessMode::OPEN_READ_ONLY),
        std::filesystem::filesystem_error
    );

    //
    // 8) listFlows skips invalid directories
    //
    {
        // manually drop a bogus folder
        auto invalidDir = domain / "not-a-valid-uuid.mxl-flow";
        REQUIRE(std::filesystem::create_directory(invalidDir));
        manager = std::make_shared<FlowManager>(domain);
        auto flows2 = manager->listFlows();
        REQUIRE(flows2.empty());
    }

    //
    // 9) listFlows on missing domain throws
    //
    std::filesystem::remove_all(domain, ec);
    REQUIRE_THROWS_AS(
        manager->listFlows(),
        std::filesystem::filesystem_error
    );

    //
    // 10) unsupported formats should be rejected
    //
    auto const badId = *uuids::uuid::from_string("44444444-4444-4444-4444-444444444444");
    REQUIRE_THROWS_AS(
        manager->createDiscreteFlow(badId, flowDef1, MXL_DATA_FORMAT_UNSPECIFIED, 1, grainRate, 128),
        std::runtime_error
    );
    REQUIRE_THROWS_AS(
        manager->createContinuousFlow(badId, flowDef2, MXL_DATA_FORMAT_VIDEO, sampleRate, 1, 4, 1024),
        std::runtime_error
    );
}