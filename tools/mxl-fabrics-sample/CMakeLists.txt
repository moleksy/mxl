

find_package(CLI11 CONFIG REQUIRED)
find_package(stduuid CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# Add the test executable
add_executable(mxl-fabrics-sample
    demo.cpp
)

target_link_libraries(mxl-fabrics-sample PRIVATE
    mxl
    stduuid
    CLI11::CLI11
    spdlog::spdlog
)

set_target_properties(mxl-fabrics-sample PROPERTIES
    INSTALL_RPATH "$ORIGIN/../lib"
)

# Install targets
install(TARGETS mxl-fabrics-sample
    RUNTIME DESTINATION bin
)
