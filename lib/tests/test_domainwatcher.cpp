#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <uuid.h>
#include <catch2/catch_test_macros.hpp>
#include "../src/internal/DomainWatcher.hpp"
#include "../src/internal/PathUtils.hpp"

using namespace mxl::lib;
namespace fs = std::filesystem;

TEST_CASE("Directory Watcher", "[directory watcher]")
{
    char const* homeDir = getenv("HOME");
    REQUIRE(homeDir != nullptr);
    fs::path domain{std::string{homeDir} + "/mxl_domain"}; // Remove that path if it exists.
    fs::remove_all(domain);

    fs::create_directories(domain);

    auto watcher = DomainWatcher{domain.native(), nullptr};

    auto const* id = "5fbec3b1-1b0f-417d-9059-8b94a47197ed";
    auto const flowDirectory = makeFlowDirectoryName(domain, id);

    fs::create_directories(flowDirectory);

    auto flowId = *uuids::uuid::from_string(id);
    auto f1Path = makeFlowDataFilePath(flowDirectory);
    auto f1 = std::ofstream{f1Path};

    auto f2Path = makeFlowAccessFilePath(flowDirectory);
    auto f2 = std::ofstream{f2Path};

    REQUIRE(1 == watcher.addFlow(flowId, WatcherType::READER));
    REQUIRE(2 == watcher.addFlow(flowId, WatcherType::READER));
    REQUIRE(1 == watcher.addFlow(flowId, WatcherType::WRITER));
    REQUIRE(2 == watcher.addFlow(flowId, WatcherType::WRITER));
    REQUIRE(1 == watcher.removeFlow(flowId, WatcherType::READER));
    REQUIRE(0 == watcher.removeFlow(flowId, WatcherType::READER));
    REQUIRE(-1 == watcher.removeFlow(flowId, WatcherType::READER));
    REQUIRE(1 == watcher.removeFlow(flowId, WatcherType::WRITER));
    REQUIRE(0 == watcher.removeFlow(flowId, WatcherType::WRITER));
    REQUIRE(-1 == watcher.removeFlow(flowId, WatcherType::WRITER));
}
