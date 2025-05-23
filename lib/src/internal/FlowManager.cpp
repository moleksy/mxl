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
#include <mxl/time.h>
#include <mxl/mxl.h>
#include <system_error>
#include "Flow.hpp"
#include "Logging.hpp"
#include "PathUtils.hpp"
#include "SharedMem.hpp"

namespace mxl::lib
{
    namespace
    {
        /**
         * Attempt to create a temporary directory to prepare a new flow.
         * The temporary name is structured in a way that prevents is from
         * clashing with directory names that belkong to established flows.
         *
         * \param[in] base the base directory, below which the temporary
         *      directory should be created,
         * \return A filesystem path referring to the newly created temporary
         *      directory.
         * \throws std::filesystem::filesystem_error if creating the temporary
         *      directory failed for whatever reason.
         */
        std::filesystem::path createTemporaryFlowDirectory(std::filesystem::path const& base)
        {
            auto pathBuffer = (base / ".mxl-tmp-XXXXXXXXXXXXXXXX").string();
            if (::mkdtemp(pathBuffer.data()) == nullptr)
            {
                throw std::filesystem::filesystem_error{
                    "Could not create temporary directory.", base, std::error_code{errno, std::generic_category()}
                };
            }
            return std::move(pathBuffer);
        }

        void publishFlowDirectory(std::filesystem::path const& source, std::filesystem::path const& dest)
        {
            permissions(source,
                std::filesystem::perms::group_read | std::filesystem::perms::group_exec | std::filesystem::perms::others_read |
                    std::filesystem::perms::others_exec,
                std::filesystem::perm_options::add);
            rename(source, dest);
        }
    }

    FlowManager::FlowManager(std::filesystem::path const& in_mxlDomain)
        : _mxlDomain{fs::canonical(in_mxlDomain)}
    {
        if (!exists(in_mxlDomain) || !is_directory(in_mxlDomain))
        {
            throw std::filesystem::filesystem_error(
                "Path does not exist or is not a directory", in_mxlDomain, std::make_error_code(std::errc::no_such_file_or_directory));
        }
    }

    FlowData::ptr FlowManager::createFlow(uuids::uuid const& flowId, std::string const& flowDef, std::size_t grainCount,
        Rational const& grainRate, std::size_t grainPayloadSize)
    {
        auto const uuidString = uuids::to_string(flowId);
        MXL_DEBUG("Create flow. id: {}, grainCount: {}, grain payload size: {}", uuidString, grainCount, grainPayloadSize);

        auto const tempDirectory = createTemporaryFlowDirectory(_mxlDomain);
        try
        {
            // Write the json file to disk.
            auto const flowJsonFile = makeFlowDescriptorFilePath(tempDirectory);
            if (auto out = std::ofstream{flowJsonFile, std::ios::out | std::ios::trunc}; out)
            {
                out << flowDef;
            }
            else
            {
                throw std::filesystem::filesystem_error("Failed to create flow resource definition.",
                        flowJsonFile, std::make_error_code(std::errc::io_error));
            }

            // Create the dummy file.
            auto readAccessFile = makeFlowAccessFilePath(tempDirectory);
            if (auto out = std::ofstream{readAccessFile, std::ios::out | std::ios::trunc}; !out)
            {
                throw std::filesystem::filesystem_error("Failed to create flow access file.",
                        readAccessFile, std::make_error_code(std::errc::file_exists));
            }

            auto flowData = std::make_shared<FlowData>();
            flowData->flow = std::make_shared<SharedMem<Flow>>();

            if (auto const flowFile =makeFlowDataFilePath(tempDirectory); !flowData->flow->open(flowFile.native(), true, AccessMode::READ_WRITE))
            {
                throw std::runtime_error("Failed to create flow shared memory.");
            }

            auto& info = flowData->flow->get()->info;
            info.version = 1;
            info.size = sizeof info;

            auto const idSpan = flowId.as_bytes();
            std::memcpy(info.id, idSpan.data(), idSpan.size());
            info.lastWriteTime = mxlGetTime();
            info.lastReadTime = info.lastWriteTime;


            info.grainRate = grainRate;
            info.grainCount = grainCount;
            info.syncCounter = 0;

            auto const grainDir = makeGrainDirectoryName(tempDirectory);
            create_directory(grainDir);

            for (auto i = std::size_t{0}; i < grainCount; ++i)
            {
                auto const grain = std::make_shared<SharedMem<Grain>>();
                auto const grainPath = makeGrainDataFilePath(grainDir, i);

                MXL_TRACE("Creating grain: {}", grainPath.string());

                // \todo Handle payload stored device memory
                auto const totalSize = std::size_t{MXL_GRAIN_PAYLOAD_OFFSET + grainPayloadSize};
                if (!grain->open(grainPath.string(), true, AccessMode::READ_WRITE, totalSize))
                {
                    throw std::runtime_error("Failed to create grain shared memory.");
                }

                auto& gInfo = grain->get()->header.info;
                gInfo.grainSize = grainPayloadSize;
                gInfo.version = 1;
                gInfo.size = sizeof gInfo;
                gInfo.deviceIndex = -1;

                flowData->grains.push_back(grain);
            }

            publishFlowDirectory(tempDirectory, makeFlowDirectoryName(_mxlDomain, uuidString));

            return flowData;
        }
        catch (...)
        {
            auto ec = std::error_code{};
            remove_all(tempDirectory, ec);
            throw;
        }
    }

    FlowData::ptr FlowManager::openFlow(uuids::uuid const& in_flowId, AccessMode in_mode)
    {
        auto uuid = uuids::to_string(in_flowId);

        auto const base = makeFlowDirectoryName(_mxlDomain, uuid);

        // Verify that the flow file exists.
        auto const flowFile = makeFlowDataFilePath(base);
        if (!exists(flowFile))
        {
            throw std::filesystem::filesystem_error("Flow file not found", flowFile, std::make_error_code(std::errc::no_such_file_or_directory));
        }

        // Open the shared memory
        auto flowData = std::make_shared<FlowData>();
        flowData->flow = std::make_shared<SharedMem<Flow>>();
        if (!flowData->flow->open(flowFile.string(), false, in_mode))
        {
            throw std::runtime_error("Failed to open flow shared memory.");
        }

        auto const grainCount = flowData->flow->get()->info.grainCount;
        auto const grainDir = makeGrainDirectoryName(base);
        if (exists(grainDir) && is_directory(grainDir))
        {
            // Open each grain
            for (auto i = 0U; i < grainCount; ++i)
            {
                auto grain = std::make_shared<SharedMem<Grain>>();
                auto const grainPath = makeGrainDataFilePath(grainDir, i);

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
            throw std::filesystem::filesystem_error(
                "Grain directory not found.", grainDir, std::make_error_code(std::errc::no_such_file_or_directory));
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

        auto const base = std::filesystem::path{_mxlDomain};
        return (remove_all(makeFlowDirectoryName(base, uuid)) != 0);
    }

    void FlowManager::garbageCollect()
    {
        MXL_WARN("Garbage collection of flows not implemented yet.");
        /// \todo Implement a garbage collection low priority thread.
    }

    std::vector<uuids::uuid> FlowManager::listFlows() const
    {
        auto base = std::filesystem::path{_mxlDomain};

        auto flowIds = std::vector<uuids::uuid>{};
        if (exists(base) && is_directory(base))
        {
            for (auto const& entry : std::filesystem::directory_iterator(_mxlDomain))
            {
                if (is_directory(entry) && (entry.path().extension() == FLOW_DIRECTORY_NAME_SUFFIX))
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
            throw std::filesystem::filesystem_error("Base directory not found.", base, std::make_error_code(std::errc::no_such_file_or_directory));
        }

        return flowIds;
    }

    FlowManager::~FlowManager()
    {
        MXL_TRACE("~FlowManager");
    }

} // namespace mxl::lib