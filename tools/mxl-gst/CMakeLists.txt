

find_package(CLI11 CONFIG REQUIRED)
find_package(stduuid CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_search_module(gstreamer IMPORTED_TARGET gstreamer-1.0)
pkg_search_module(gstreamer-app IMPORTED_TARGET gstreamer-app-1.0)
pkg_search_module(gstreamer-video IMPORTED_TARGET gstreamer-video-1.0)

if(gstreamer_FOUND)

# Add the test executable
add_executable(mxl-gst-videotestsrc
    videotestsrc.cpp
    ../../lib/src/internal/FlowParser.cpp
)

add_executable(mxl-gst-videotestsrcv2
    videotestsrcv2.cpp
    ../../lib/src/internal/FlowParser.cpp
)
target_link_libraries(mxl-gst-videotestsrc PRIVATE
    mxl
    stduuid
    CLI11::CLI11
    spdlog::spdlog
    PkgConfig::gstreamer
    PkgConfig::gstreamer-app
    PkgConfig::gstreamer-video
)
target_link_libraries(mxl-gst-videotestsrcv2 PRIVATE
    mxl
    stduuid
    CLI11::CLI11
    spdlog::spdlog
    PkgConfig::gstreamer
    PkgConfig::gstreamer-app
    PkgConfig::gstreamer-video
)

# Add the test executable
add_executable(mxl-gst-videosink
    videosink.cpp
    ../../lib/src/internal/FlowParser.cpp
)

target_link_libraries(mxl-gst-videosink PRIVATE
    mxl
    stduuid
    CLI11::CLI11
    spdlog::spdlog
    PkgConfig::gstreamer
    PkgConfig::gstreamer-app
    PkgConfig::gstreamer-video
)

set_target_properties(mxl-gst-videosink mxl-gst-videotestsrc mxl-gst-videotestsrcv2 PROPERTIES
    INSTALL_RPATH "$ORIGIN/../lib"
)

# Install targets
install(TARGETS mxl-gst-videosink mxl-gst-videotestsrc mxl-gst-videotestsrcv2
    RUNTIME DESTINATION bin
)

endif()