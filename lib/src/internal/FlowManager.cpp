#include "FlowManager.hpp"
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <uuid.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <system_error>
#include "Flow.hpp"
#include "Logging.hpp"
#include "SharedMem.hpp"

namespace mxl::lib
{

    namespace fs = std::filesystem;

    FlowManager::FlowManager(std::filesystem::path const& in_mxlDomain)
        : _mxlDomain{fs::canonical(in_mxlDomain)}
    {
        if (!fs::exists(in_mxlDomain) || !fs::is_directory(in_mxlDomain))
        {
            throw fs::filesystem_error(
                "Path does not exist or is not a directory", in_mxlDomain, std::make_error_code(std::errc::no_such_file_or_directory));
        }
    }

    FlowData::ptr FlowManager::createFlow(uuids::uuid const& in_flowId, std::string const& in_flowDef, size_t in_grainCount,
        Rational const& in_grainRate, size_t in_grainPayloadSize)
    {
        auto uuid = uuids::to_string(in_flowId);
        MXL_DEBUG("Create flow. id: {}, grainCount: {}, grain payload size: {}", uuid, in_grainCount, in_grainPayloadSize);

        auto const flowDirectory = _mxlDomain / (uuid + ".mxl-flow");
        if (!fs::exists(flowDirectory))
        {
            fs::create_directory(flowDirectory);
        }
        else
        {
            throw fs::filesystem_error("Flow file conflict. File already exists", flowDirectory, std::make_error_code(std::errc::file_exists));
        }

        auto flowJsonFile = flowDirectory / ".json";
        if (fs::exists(flowJsonFile))
        {
            throw fs::filesystem_error(
                "Flow description file conflict. File already exists", flowJsonFile, std::make_error_code(std::errc::file_exists));
        }

        auto grainDir = flowDirectory / "grains";
        if (fs::exists(grainDir))
        {
            throw fs::filesystem_error("Grain directory conflict. Directory already exists", grainDir, std::make_error_code(std::errc::file_exists));
        }

        // Write the json file to disk.
        if (auto out = std::ofstream{flowJsonFile, std::ios::out | std::ios::trunc}; out)
        {
            out << in_flowDef;
        }
        else
        {
            throw fs::filesystem_error("Failed to open file", flowJsonFile, std::make_error_code(std::errc::io_error));
        }

        auto readAccessFile = flowDirectory / ".access";
        if (!fs::exists(readAccessFile))
        {
            // create the dummy file.
            std::ofstream out{readAccessFile, std::ios::out | std::ios::trunc};
        }
        else
        {
            throw fs::filesystem_error("Read access file already exists", readAccessFile, std::make_error_code(std::errc::file_exists));
        }

        auto flowData = std::make_shared<FlowData>();
        flowData->flow = std::make_shared<SharedMem<Flow>>();

        if (auto const flowFile = flowDirectory / "data";
            fs::exists(flowFile) || !flowData->flow->open(flowFile.native(), true, AccessMode::READ_WRITE))
        {
            throw std::runtime_error("Failed to create flow shared memory.");
        }

        auto& info = flowData->flow->get()->info;
        info.version = 1;
        info.size = sizeof(FlowInfo);
        info.grainRate = in_grainRate;
        info.grainCount = in_grainCount;
        info.syncCounter = 0;

        auto idSpan = in_flowId.as_bytes();
        std::memcpy(info.id, idSpan.data(), idSpan.size());

        fs::create_directory(grainDir);
        for (size_t i = 0; i < in_grainCount; i++)
        {
            auto grain = std::make_shared<SharedMem<Grain>>();
            auto grainPath = grainDir / fmt::format("data.{}", i);

            MXL_TRACE("Creating grain: {}", grainPath.string());

            // \todo Handle payload stored device memory
            size_t totalSize = MXL_GRAIN_PAYLOAD_OFFSET + in_grainPayloadSize;
            if (!grain->open(grainPath.string(), true, AccessMode::READ_WRITE, totalSize))
            {
                throw std::runtime_error("Failed to create grain shared memory.");
            }

            auto& gInfo = grain->get()->header.info;
            gInfo.grainSize = in_grainPayloadSize;
            gInfo.version = 1;
            gInfo.size = sizeof(GrainInfo);
            gInfo.deviceIndex = -1;
            flowData->grains.push_back(grain);
        }

        return flowData;
    }

    FlowData::ptr FlowManager::openFlow(uuids::uuid const& in_flowId, AccessMode in_mode)
    {
        auto uuid = uuids::to_string(in_flowId);

        auto const base = fs::path{_mxlDomain} / (uuid + ".mxl-flow");

        // Verify that the flow file exists.
        auto flowFile = base / "data";
        if (!fs::exists(flowFile))
        {
            throw fs::filesystem_error("Flow file not found", flowFile, std::make_error_code(std::errc::no_such_file_or_directory));
        }

        // Open the shared memory
        auto flowData = std::make_shared<FlowData>();
        flowData->flow = std::make_shared<SharedMem<Flow>>();
        if (!flowData->flow->open(flowFile.string(), false, in_mode))
        {
            throw std::runtime_error("Failed to open flow shared memory.");
        }

        size_t grainCount = flowData->flow->get()->info.grainCount;
        auto grainDir = base / "grains";
        if (fs::exists(grainDir) && fs::is_directory(grainDir))
        {
            // Open each grains
            for (size_t i = 0; i < grainCount; i++)
            {
                auto grain = std::make_shared<SharedMem<Grain>>();
                auto grainPath = grainDir / fmt::format("data.{}", i);
                MXL_TRACE("Opening grain: {}", grainPath.string());
                if (!grain->open(grainPath.string(), false, in_mode))
                {
                    throw std::runtime_error("Failed to open grain shared memory.");
                }
                flowData->grains.push_back(grain);
            }
        }
        else
        {
            throw fs::filesystem_error("Grain directory not found.", grainDir, std::make_error_code(std::errc::no_such_file_or_directory));
        }

        return flowData;
    }

    void FlowManager::closeFlow(FlowData::ptr in_flowData)
    {
        // Explicitly close all resources.
        in_flowData->flow->close();
        for (auto grain : in_flowData->grains)
        {
            grain->close();
        }

        in_flowData->flow.reset();
        in_flowData->grains.clear();
    }

    bool FlowManager::deleteFlow(uuids::uuid const& in_flowId, FlowData::ptr in_flowData)
    {
        auto uuid = uuids::to_string(in_flowId);
        MXL_TRACE("Delete flow: {}", uuid);

        // First. close the flow if opened.
        if (in_flowData && in_flowData->flow)
        {
            closeFlow(in_flowData);
        }

        auto const base = fs::path{_mxlDomain};
        auto const found = (std::filesystem::remove_all(base / (uuid + ".mxl-flow")) != 0);
        return found;
    }

    void FlowManager::garbageCollect()
    {
        MXL_WARN("Garbage collection of flows not implemented yet.");
        /// \todo Implement a garbage collection low priority thread.
    }

    std::vector<uuids::uuid> FlowManager::listFlows() const
    {
        auto base = fs::path{_mxlDomain};

        auto flowIds = std::vector<uuids::uuid>{};
        if (exists(base) && is_directory(base))
        {
            for (auto const& entry : std::filesystem::directory_iterator(_mxlDomain))
            {
                if (is_directory(entry) && (entry.path().extension() == ".mxl-flow"))
                {
                    // this looks like a uuid. try to parse it an confirm it is valid.
                    auto id = uuids::uuid::from_string(entry.path().stem().string());
                    if (id.has_value())
                    {
                        flowIds.push_back(*id);
                    }
                }
            }
        }
        else
        {
            throw fs::filesystem_error("Base directory not found.", base, std::make_error_code(std::errc::no_such_file_or_directory));
        }

        return flowIds;
    }

    FlowManager::~FlowManager()
    {
        MXL_TRACE("~FlowManager");
    }

} // namespace mxl::lib