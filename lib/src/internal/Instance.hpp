#pragma once

#include <cstdint>
#include <atomic>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <uuid.h>
#include <mxl/mxl.h>
#include <unordered_map>
#include "DomainWatcher.hpp"
#include "FlowManager.hpp"
#include "FlowReader.hpp"
#include "FlowWriter.hpp"

namespace mxl::lib
{

    // Strong typedefs for reader and writer IDs
    enum class FlowReaderId : uint32_t
    {
    };
    enum class FlowWriterId : uint32_t
    {
    };

    ///
    /// Manages mxl resources allocated by the SDK user.  Represents a single mxfInstance.
    ///
    class Instance
    {
    protected:
        /// Maps the flow reader id to FlowReaders.
        std::unordered_map<FlowReaderId, std::shared_ptr<FlowReader>> _readers;
        /// Maps flow uuids to flow readers.
        std::unordered_map<uuids::uuid, std::shared_ptr<FlowReader>> _readersByUUID;
        /// Holds the writer instances allocated by the SDK user
        std::unordered_map<FlowWriterId, std::shared_ptr<FlowWriter>> _writers;
        /// Maps flow uuids to flow writers.
        std::unordered_map<uuids::uuid, std::shared_ptr<FlowWriter>> _writersByUUID;

        void fileChangedCallback(uuids::uuid const& in_flowId, WatcherType in_type);

        /// Simple 'reader' id counter.  This is just a dumb counter that is incremented for each reader added. These IDs only exist in the context of
        /// the sdk instance.
        uint32_t _readerCounter = 0;

        /// Simple 'writer' id counter.  Use a different range than reader counter in order to avoid dumb mistakes.
        uint32_t _writerCounter = std::numeric_limits<uint32_t>::max() / 2;

        /// Protects the maps
        std::mutex _mutex;

        /// For future use.
        std::string _options;

        /// Performs flow CRUD operations
        FlowManager::ptr _flowManager;

        /// Ring buffer history duration in milliseconds;
        uint32_t _historyDuration = 100;

        DomainWatcher::ptr _watcher;

        std::atomic_bool _stopping = false;

    public:
        ///
        /// Creates an instance
        /// \param in_mxlDomain The directory where the shared memory files will be created
        /// \param in_jsonOptions Additional options. \todo Not implemented yet.
        ///
        Instance(std::filesystem::path const& in_mxlDomain, std::string const& in_jsonOptions);

        /// Dtor. Release all readers and writers.
        virtual ~Instance();

        /// Delete Copy Constructor
        Instance(Instance const&) = delete;

        /// Delete Copy Assignment
        Instance& operator=(Instance const&) = delete;

        ///
        /// Create a flow
        ///
        /// \param in_flowDef The json flow definition according to the NMOS Flow Resource json schema
        /// \return The created flow resources in shared memory
        /// \throw std::runtime_error On any error (parse exception, shared memory conflicts, etc)
        ///
        FlowData::ptr createFlow(std::string const& in_flowDef);

        /// Delete a flow by id
        ///
        /// \param in_id The flow id
        /// \return false if the flow was not found.
        bool deleteFlow(uuids::uuid const& in_id);

        ///
        /// create a FlowReader.
        /// \param in_flowId The id of the flow to read
        /// \return the id of the flow reader created
        ///
        FlowReaderId createFlowReader(std::string const& in_flowId);

        ///
        /// Get a FlowReader by id
        /// \param in_id The reader id
        /// \return The flow reader if found or a null shared pointer if not found.
        ///
        std::shared_ptr<FlowReader> getReader(FlowReaderId in_id);

        ///
        /// Removes a FlowReader by id
        /// \param in_id The reader id
        /// \return True if found
        ///
        bool removeReader(FlowReaderId in_id);

        ///
        /// Create a FlowWriter.
        /// \param in_flowId The id of the flow to write
        /// \return The unique instance id of this writer.
        ///
        FlowWriterId createFlowWriter(std::string const& in_flowId);

        ///
        /// Get a FlowWriter by id
        /// \param in_id The writer id
        /// \return The flow writer if found or a null shared pointer if not found.
        ///
        std::shared_ptr<FlowWriter> getWriter(FlowWriterId in_id);

        ///
        /// Removes a FlowWriter by id
        /// \param in_id The writer id
        /// \return True if found
        ///
        bool removeWriter(FlowWriterId in_id);

        ///
        /// Garbage collect the inactive flows.  This will remove any flows that are not being used by any readers or writers.
        /// \return The number of flows that were removed.
        ///
        ::size_t garbageCollect() const;
    };

    // POD struct to hold an instance and that can be easily
    // cast to and from void* across the C interface.
    struct InstanceInternal
    {
        std::unique_ptr<Instance> instance;
    };

    /// Utility function to convert from a C mxlInstance to a C++ Instance class
    Instance* to_Instance(mxlInstance in_instance);

    /// Utility function to convert from a C mxlFlowReader to a C++ FlowReaderId type
    FlowReaderId to_FlowReaderId(mxlFlowReader in_reader);

    /// Utility function to convert from a C mxlFlowWriter to a C++ FlowWriterId type
    FlowWriterId to_FlowWriterId(mxlFlowWriter in_writer);

} // namespace mxl::lib