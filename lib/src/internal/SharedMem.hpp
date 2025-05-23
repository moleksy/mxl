#pragma once

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <array>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <utime.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "Logging.hpp"

namespace mxl::lib
{

    enum class AccessMode
    {
        READ_ONLY,
        READ_WRITE
    };

    ///
    /// Utility class that allocates and places a structure in shared memory
    ///
    template<typename T>
    class SharedMem
    {
    private:
        //
        // Pointer to the mapped region
        //
        T* _data = nullptr;

        //
        // File descriptor of the shared memory object
        //
        int _fd = -1;
        uint64_t _mappedSize = 0;

    public:
        typedef std::shared_ptr<SharedMem<T>> ptr;

        ~SharedMem();

        ///
        /// Open (and optionally create) a shared memory mapping
        ///
        /// \param in_path The memory mapping path
        /// \param in_create Optionally create the file if it does not exist.
        /// \param in_mode The memory mapping access mode
        /// \param in_extraSize Add this size to the shared memory in addition to sizeof(T)
        /// \return true on success. false on conflicts or failure to create resources on disk.
        ///
        bool open(std::string const& in_path, bool in_create, AccessMode in_mode, size_t in_extraSize = 0);

        ///
        /// Unmaps the memory and closes the underlying file.
        ///
        void close();

        ///
        /// Update the attributes of the file
        ///
        void touch();

        ///
        /// Accessor for the mapped data
        /// \return the mapped data
        T* get() const;
    };

    template<typename T>
    void SharedMem<T>::touch()
    {
        // Update the file times
        std::array<timespec, 2> times;
        times[0].tv_sec = 0; // Set access time to current time
        times[0].tv_nsec = UTIME_NOW;
        times[1].tv_sec = 0; // Set modification time to current time
        times[1].tv_nsec = UTIME_NOW;

        if (::futimens(_fd, times.data()) == -1)
        {
            MXL_ERROR("Failed to update file times");
        }
    }

    template<typename T>
    bool SharedMem<T>::open(std::string const& in_path, bool in_create, AccessMode in_mode, size_t in_payloadSize)
    {
        if (_fd != -1)
        {
            // Already open
            return false;
        }

        auto created = false;

        if (in_create && in_mode == AccessMode::READ_ONLY)
        {
            MXL_ERROR("Cannot create a read-only shared memory object");
            return false;
        }

        // open mode flags when we are creating the file (implicitly in readwrite mode) and it does not exist
        int omode_create = O_EXCL | O_CREAT | O_RDWR | O_CLOEXEC;

        // if the file already exists, then we need to tweak the open mode based on the requested access mode
        int omode_fallback = (in_mode == AccessMode::READ_WRITE) ? O_RDWR | O_CLOEXEC : O_RDONLY | O_CLOEXEC;

        if (in_create && (_fd = ::open(in_path.c_str(), omode_create, 0664)) != -1)
        {
            // Ensure the file is large enough to hold the shared data
            if (ftruncate(_fd, sizeof(T) + in_payloadSize) == -1)
            {
                throw std::runtime_error("ftruncate failed");
            }
            created = true;
        }
        else if ((_fd = ::open(in_path.c_str(), omode_fallback)) == -1)
        {
            MXL_ERROR("Open failed : {}, {}", in_path, strerror(errno));
            return false;
        }

        if (in_mode == AccessMode::READ_WRITE)
        {
            // Try to obtain a shared lock on the the file descriptor
            // This is useful to detect if ringbufers and flows are 'stale' (if the writer processes are still using it)
            if (flock(_fd, LOCK_SH | LOCK_NB) < 0)
            {
                MXL_WARN("Failed to aquire a shared advisory lock on file: {}, {}", in_path, strerror(errno));
            }
        }

        // mmap the whole file.
        _mappedSize = std::filesystem::file_size(in_path);

        int protFlags = (in_mode == AccessMode::READ_ONLY) ? PROT_READ : PROT_READ | PROT_WRITE;
        auto const shared_data_buffer = ::mmap(NULL, _mappedSize, protFlags, MAP_FILE | MAP_SHARED, _fd, 0);
        if (shared_data_buffer == MAP_FAILED)
        {
            MXL_ERROR("mmap failed : {}, {}", in_path, strerror(errno));
            return false;
        }

        if (created)
        {
            _data = new (shared_data_buffer) T{};
        }
        else
        {
            _data = std::launder(static_cast<T*>(shared_data_buffer));
        }

        return true;
    }

    template<typename T>
    SharedMem<T>::~SharedMem()
    {
        close();
    }

    template<typename T>
    void SharedMem<T>::close()
    {
        if (_fd != -1)
        {
            ::munmap(_data, _mappedSize);
            ::close(_fd);
            _fd = -1;
            _data = nullptr;
            _mappedSize = 0;
        }
    }

    template<typename T>
    T* SharedMem<T>::get() const
    {
        if (_fd == -1)
        {
            throw std::runtime_error("No memory map.");
        }

        return _data;
    }

} // namespace mxl::lib
