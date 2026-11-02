cmake_minimum_required(VERSION 3.14)
project(LogosAgentModulePlugin LANGUAGES CXX)

# Include the Logos Module CMake helper.
# For nix builds, this is provided via LOGOS_MODULE_BUILDER_ROOT.
if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/LogosModule.cmake")
    include(cmake/LogosModule.cmake)
else()
    message(FATAL_ERROR "LogosModule.cmake not found. Set LOGOS_MODULE_BUILDER_ROOT.")
endif()

# Define the module. The generated glue (plugin, C-ABI exports) is compiled
# automatically from the impl header by logos-module-builder.
logos_module(
    NAME agent_module
    SOURCES
        src/agent_module_impl.h
        src/agent_module_impl.cpp
)

# Precompile heavy third-party headers. logos_lp_client.h alone costs ~2.3s
# to parse (Boost/Qt), which dominated every incremental compile.
if(TARGET agent_module_module_plugin)
    target_precompile_headers(agent_module_module_plugin PRIVATE
        <logos_lp_client.h>
        <logos_module_context.h>
        <openssl/evp.h>
        <openssl/rand.h>
        <functional>
        <fstream>
        <iomanip>
        <sstream>
        <string>
        <unordered_map>
        <vector>
    )
endif()
