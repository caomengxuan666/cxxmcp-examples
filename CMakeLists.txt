cmake_minimum_required(VERSION 3.23)

project(cxxmcp_examples
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

option(CXXMCP_EXAMPLES_USE_ADJACENT_SDK
    "Use ../MCPServer.cpp as the cxxmcp SDK source tree when present" ON)
option(CXXMCP_ENABLE_INSPECTOR_SMOKE
    "Enable the npm-based MCP Inspector smoke test for validation fixtures" OFF)
set(CXXMCP_EXAMPLES_SDK_SOURCE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../MCPServer.cpp"
    CACHE PATH "Path to a cxxmcp SDK source tree")

if(CXXMCP_EXAMPLES_USE_ADJACENT_SDK
   AND EXISTS "${CXXMCP_EXAMPLES_SDK_SOURCE_DIR}/CMakeLists.txt")
    set(CXXMCP_BUILD_SDK ON CACHE BOOL "" FORCE)
    set(CXXMCP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(CXXMCP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(CXXMCP_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(CXXMCP_BUILD_GATEWAY ON CACHE BOOL "" FORCE)
    set(CXXMCP_ENABLE_HTTP ON CACHE BOOL "")
    add_subdirectory("${CXXMCP_EXAMPLES_SDK_SOURCE_DIR}"
                     "${CMAKE_BINARY_DIR}/_deps/cxxmcp")
else()
    find_package(cxxmcp CONFIG REQUIRED)
endif()

function(cxxmcp_example target_name)
    target_compile_features("${target_name}" PRIVATE cxx_std_20)
    target_link_libraries("${target_name}" PRIVATE cxxmcp::sdk)
endfunction()

enable_testing()

function(cxxmcp_register_test target_name)
    add_test(NAME "${target_name}" COMMAND "${target_name}")
    set_tests_properties("${target_name}" PROPERTIES TIMEOUT 60)
endfunction()

add_subdirectory(examples)
add_subdirectory(validation)
