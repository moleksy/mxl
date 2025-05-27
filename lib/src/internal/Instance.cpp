#include "Instance.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <uuid.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include <picojson/picojson.h>
#include <spdlog/cfg/env.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include "DomainWatcher.hpp"
#include "FlowManager.hpp"
#include "FlowParser.hpp"
#include "FlowReader.hpp"
#include "FlowWriter.hpp"
#include "Logging.hpp"
#include "PathUtils.hpp"
#include "PosixFlowReader.hpp"
#include "PosixFlowWriter.hpp"

namespace mxl::lib
{

    namespace
    {

        std::once_flag loggingFlag;

        void initializeLogging()
        {
            auto console = spdlog::stdout_color_mt("console");
            spdlog::set_default_logger(console);
            spdlog::cfg::load_env_levels("MXL_LOG_LEVEL");
        }

    } // namespace

    Instance::Instance(std::filesystem::path const& in_mxlDomain, std::string const& in_options)
        : _options(in_options)
    {
        std::call_once(loggingFlag, [&]() { initializeLogging(); });
        _flowManager = std::make_shared<FlowManager>(in_mxlDomain);
        _watcher = std::make_shared<DomainWatcher>(in_mxlDomain,
            [this](auto const& in_uuid, auto in_type) { fileChangedCallback(in_uuid, in_type); });
        MXL_DEBUG("Instance created. MXL Domain: {}", in_mxlDomain.string());
    }

    Instance::~Instance()
    {
        _stopping = true;
        _watcher->stop();
        std::lock_guard<std::mutex> lock(_mutex);
        _readers.clear();
        _readersByUUID.clear();
        _writers.clear();
        _watcher.reset();
        MXL_DEBUG("Instance destroyed.");
        spdlog::default_logger()->flush();
    }

    void Instance::fileChangedCallback(uuids::uuid const& in_flowId, WatcherType in_type)
    {
        if (!_stopping)
        {
            std::lock_guard<std::mutex> lock(_mutex);

            // Someone wrote to the flow. let the readers know.
            if (in_type == WatcherType::WRITER)

            {
                // Someone read the grain and touched the ".access" file.  let update the last read count.
                auto found = _writersByUUID.find(in_flowId);
                if (found != _writersByUUID.end())
                {
                    found->second->flowRead();
                }
            }
        }
    }

    FlowReaderId Instance::createFlowReader(std::string const& in_flowId)
    {
        auto id = uuids::uuid::from_string(in_flowId);
        auto reader = std::make_shared<PosixFlowReader>(_flowManager);
        if (!reader->open(*id))
        {
            throw std::runtime_error("Failed to open flow " + in_flowId);
        }

        _watcher->addFlow(*id, WatcherType::READER);

        std::lock_guard<std::mutex> lock(_mutex);
        FlowReaderId const index{_readerCounter++};
        _readers[index] = reader;
        _readersByUUID[*id] = reader;
        return index;
    }

    std::shared_ptr<FlowReader> Instance::getReader(FlowReaderId in_id)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto found = _readers.find(in_id);
        if (found != _readers.end())
        {
            return found->second;
        }
        else
        {
            return nullptr;
        }
    }

    bool Instance::removeReader(FlowReaderId in_id)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto found = _readers.find(in_id);
        if (found != _readers.end())
        {
            auto reader = (*found).second;
            auto flowId = reader->getId();

            _watcher->removeFlow((*found).second->getId(), WatcherType::READER);
            (*found).second->close();
            _readers.erase(found);

            auto byId = _readersByUUID.find(flowId);
            if (byId != _readersByUUID.end())
            {
                _readersByUUID.erase(byId);
            }
            else
            {
                MXL_WARN("This should not happen.");
            }

            return true;
        }
        else
        {
            return false;
        }
    }

    FlowWriterId Instance::createFlowWriter(std::string const& in_flowId)
    {
        auto id = uuids::uuid::from_string(in_flowId);
        auto writer = std::make_shared<PosixFlowWriter>(_flowManager);
        writer->open(*id);

        _watcher->addFlow(*id, WatcherType::WRITER);

        std::lock_guard<std::mutex> lock(_mutex);
        FlowWriterId const index{_writerCounter++};
        _writers[index] = writer;
        _writersByUUID[*id] = writer;
        return index;
    }

    std::shared_ptr<FlowWriter> Instance::getWriter(FlowWriterId in_id)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto found = _writers.find(in_id);
        if (found != _writers.end())
        {
            return found->second;
        }
        else
        {
            return nullptr;
        }
    }

    bool Instance::removeWriter(FlowWriterId in_id)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto found = _writers.find(in_id);
        if (found != _writers.end())
        {
            _watcher->removeFlow(found->second->getId(), WatcherType::WRITER);

            (*found).second->close();
            _writers.erase(found);
            return true;
        }
        else
        {
            return false;
        }
    }

    FlowData::ptr Instance::createFlow(std::string const& in_flowDef)
    {
        // Parse the json flow resource
        FlowParser parser{in_flowDef};
        // Read the mandatory grain_rate field
        Rational grainRate = parser.getGrainRate();
        // Read the mandatory id field
        auto id = parser.getId();
        // Compute the grain count based on our configured history duration
        size_t grainCount = _historyDuration / (1000 * grainRate.denominator / grainRate.numerator);

        // Create the flow using the flow manager
        auto flowData = _flowManager->createFlow(id, in_flowDef, grainCount, grainRate, parser.getPayloadSize());
        auto info = flowData->flow->get()->info;

        // Populate the flowinfo structure. This structure will be visible through the C api.
        info.version = 1;
        info.size = sizeof(FlowInfo);
        auto idSpan = id.as_bytes();
        std::memcpy(info.id, idSpan.data(), idSpan.size());
        info.flags = 0;
        info.headIndex = 0;
        info.grainRate = grainRate;
        info.grainCount = grainCount;

        info.lastWriteTime = mxlGetTime();
        info.lastReadTime = info.lastWriteTime;

        return flowData;
    }

    bool Instance::deleteFlow(uuids::uuid const& in_id)
    {
        return _flowManager->deleteFlow(in_id, nullptr);
    }

    // This function is perfomed in a 'collaborative best effort' way.
    // Exceptions thrown should not be propagated to the caller and cause distruptions to the application.
    // On error the function will return 0 and log the error
    ::size_t Instance::garbageCollect() const
    {
        ::size_t count = 0;

        try
        {
            auto base = std::filesystem::path{_flowManager->getDomain()};
            if (exists(base) && is_directory(base))
            {
                for (auto const& entry : std::filesystem::directory_iterator{base})
                {
                    if (is_directory(entry) && (entry.path().extension() == mxl::lib::FLOW_DIRECTORY_NAME_SUFFIX))
                    {
                        // Try to obtain an exclusive lock on the flow data file.  If we can obtain one it means that no
                        // other process is writing to the flow.
                        auto const flowDataFile = mxl::lib::makeFlowDataFilePath(_flowManager->getDomain(), entry.path().stem().string());

                        // Check if the flow data file exists
                        if (!std::filesystem::exists(flowDataFile))
                        {
                            MXL_DEBUG("Flow data file {} does not exist", flowDataFile.string());
                            continue;
                        }

                        // Open a file descriptor to the flow data file
                        int flags = O_RDONLY | O_CLOEXEC;
#ifndef __APPLE__
                        flags |= O_NOATIME;
#endif
                        int fd = ::open(flowDataFile.c_str(), flags);
                        // Try to obtain an exclusive lock on the file descriptor. Do not block if the lock cannot be obtained.
                        bool active = ::flock(fd, LOCK_EX | LOCK_NB) < 0;
                        ::close(fd);

                        // The flow is not active.  remove it (the folder and everything in it)
                        if (!active)
                        {
                            std::error_code ec;
                            std::filesystem::remove_all(entry.path(), ec);
                            if (ec)
                            {
                                MXL_DEBUG("Failed to remove '{}': {} (error code {})", entry.path().string(), ec.message(), ec.value());
                            }
                            else
                            {
                                count++;
                            }
                        }
                    }
                }
            }
            else
            {
                MXL_DEBUG("MXL domain {} does not exist or is not a directory", base.string());
            }
        }
        catch (std::exception const& e)
        {
            MXL_DEBUG("Failed to perform garbage collection: {}", e.what());
        }
        catch (...)
        {
            MXL_DEBUG("Failed to perform garbage collection");
        }
        return count;
    }

    Instance* to_Instance(mxlInstance in_instance)
    {
        return (in_instance != nullptr) ? reinterpret_cast<InstanceInternal*>(in_instance)->instance.get() : nullptr;
    }

    FlowReaderId to_FlowReaderId(mxlFlowReader in_reader)
    {
        return static_cast<FlowReaderId>(reinterpret_cast<std::uintptr_t>(in_reader));
    }

    FlowWriterId to_FlowWriterId(mxlFlowWriter in_writer)
    {
        return static_cast<FlowWriterId>(reinterpret_cast<std::uintptr_t>(in_writer));
    }

} // namespace mxl::lib
