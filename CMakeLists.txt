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
