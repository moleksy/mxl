#include "PathUtils.hpp"
#include <fmt/format.h>

namespace mxl::lib
{
    std::filesystem::path makeFlowDirectoryName(std::filesystem::path const& domain, std::string const& uuid)
    {
        return domain / (uuid + FLOW_DIRECTORY_NAME_SUFFIX);
    }

    std::filesystem::path makeFlowDataFilePath(std::filesystem::path const& flowDirectory)
    {
        return flowDirectory / FLOW_DATA_FILE_NAME;
    }

    std::filesystem::path makeFlowDescriptorFilePath(std::filesystem::path const& flowDirectory)
    {
        return flowDirectory / FLOW_DESCRIPTOR_FILE_NAME;
    }

    std::filesystem::path makeFlowAccessFilePath(std::filesystem::path const& flowDirectory)
    {
        return flowDirectory / FLOW_ACCESS_FILE_NAME;
    }

    std::filesystem::path makeGrainDirectoryName(std::filesystem::path const& flowDirectory)
    {
        return flowDirectory / GRAIN_DIRECTORY_NAME;
    }

    std::filesystem::path makeGrainDataFilePath(std::filesystem::path const& grainDirectory, unsigned int index)
    {
        return grainDirectory / fmt::format("{}.{}", GRAIN_DATA_FILE_NAME_STEM, index);
    }
}
