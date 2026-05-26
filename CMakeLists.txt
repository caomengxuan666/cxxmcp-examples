cmake_minimum_required(VERSION 3.23)

project(cxxmcp_examples
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CXXMCP_EXAMPLES_USE_ADJACENT_SDK
    "Use ../MCPServer.cpp as the cxxmcp SDK source tree when present" ON)
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
    add_subdirectory("${CXXMCP_EXAMPLES_SDK_SOURCE_DIR}"
                     "${CMAKE_BINARY_DIR}/_deps/cxxmcp")
else()
    find_package(cxxmcp CONFIG REQUIRED)
endif()

function(cxxmcp_example target_name)
    target_compile_features("${target_name}" PRIVATE cxx_std_20)
    target_link_libraries("${target_name}" PRIVATE cxxmcp::sdk)
endfunction()

add_executable(cxxmcp_minimal_stdio_server src/minimal_stdio_server.cpp)
cxxmcp_example(cxxmcp_minimal_stdio_server)

add_executable(cxxmcp_typed_tool_server src/typed_tool_server.cpp)
cxxmcp_example(cxxmcp_typed_tool_server)

add_executable(cxxmcp_streamable_http_client src/streamable_http_client.cpp)
cxxmcp_example(cxxmcp_streamable_http_client)

add_executable(cxxmcp_elicitation_client src/elicitation_client.cpp)
cxxmcp_example(cxxmcp_elicitation_client)

add_executable(cxxmcp_graceful_shutdown src/graceful_shutdown.cpp)
cxxmcp_example(cxxmcp_graceful_shutdown)

add_executable(cxxmcp_timeout_cancellation_client
    src/timeout_cancellation_client.cpp)
cxxmcp_example(cxxmcp_timeout_cancellation_client)

add_executable(cxxmcp_client_inbound_cancellation_matrix
    src/client_inbound_cancellation_matrix.cpp)
cxxmcp_example(cxxmcp_client_inbound_cancellation_matrix)

add_executable(cxxmcp_client_callbacks_matrix src/client_callbacks_matrix.cpp)
cxxmcp_example(cxxmcp_client_callbacks_matrix)

add_executable(cxxmcp_transport_stdio_matrix src/transport_stdio_matrix.cpp)
cxxmcp_example(cxxmcp_transport_stdio_matrix)

add_executable(cxxmcp_process_stdio_client_probe src/process_stdio_client_probe.cpp)
cxxmcp_example(cxxmcp_process_stdio_client_probe)
target_compile_definitions(cxxmcp_process_stdio_client_probe
    PRIVATE CXXMCP_EXAMPLES_CHILD_EXE="$<TARGET_FILE:cxxmcp_minimal_stdio_server>")
add_dependencies(cxxmcp_process_stdio_client_probe cxxmcp_minimal_stdio_server)

add_executable(cxxmcp_async_request_matrix src/async_request_matrix.cpp)
cxxmcp_example(cxxmcp_async_request_matrix)

add_executable(cxxmcp_policy_subscription_matrix src/policy_subscription_matrix.cpp)
cxxmcp_example(cxxmcp_policy_subscription_matrix)

add_executable(cxxmcp_extension_plugin_adapter_matrix src/extension_plugin_adapter_matrix.cpp)
target_compile_features(cxxmcp_extension_plugin_adapter_matrix PRIVATE cxx_std_20)
target_link_libraries(cxxmcp_extension_plugin_adapter_matrix
    PRIVATE cxxmcp::sdk cxxmcp::plugin_sdk cxxmcp::adapters)


if(TARGET cxxmcp::gateway OR TARGET mcp_gateway)
    add_executable(cxxmcp_http_gateway_runtime_matrix src/http_gateway_runtime_matrix.cpp)
    target_compile_features(cxxmcp_http_gateway_runtime_matrix PRIVATE cxx_std_20)
    if(TARGET cxxmcp::gateway)
        target_link_libraries(cxxmcp_http_gateway_runtime_matrix
            PRIVATE cxxmcp::gateway cxxmcp::sdk)
    else()
        target_link_libraries(cxxmcp_http_gateway_runtime_matrix
            PRIVATE mcp_gateway cxxmcp::sdk)
    endif()
    target_compile_definitions(cxxmcp_http_gateway_runtime_matrix
        PRIVATE CXXMCP_EXAMPLES_CHILD_EXE="$<TARGET_FILE:cxxmcp_minimal_stdio_server>")
    add_dependencies(cxxmcp_http_gateway_runtime_matrix cxxmcp_minimal_stdio_server)
endif()
add_executable(cxxmcp_server_to_client_context_matrix src/server_to_client_context_matrix.cpp)
cxxmcp_example(cxxmcp_server_to_client_context_matrix)

add_executable(cxxmcp_handler_interface_matrix src/handler_interface_matrix.cpp)
cxxmcp_example(cxxmcp_handler_interface_matrix)

add_executable(cxxmcp_native_server_transport_matrix src/native_server_transport_matrix.cpp)
cxxmcp_example(cxxmcp_native_server_transport_matrix)

add_executable(cxxmcp_rich_content_cancellation_matrix src/rich_content_cancellation_matrix.cpp)
cxxmcp_example(cxxmcp_rich_content_cancellation_matrix)
add_executable(cxxmcp_direct_http_legacy_sse_matrix src/direct_http_legacy_sse_matrix.cpp)
cxxmcp_example(cxxmcp_direct_http_legacy_sse_matrix)
add_executable(cxxmcp_http_auth_lite_matrix src/http_auth_lite_matrix.cpp)
cxxmcp_example(cxxmcp_http_auth_lite_matrix)

add_executable(cxxmcp_pagination_cursor_matrix src/pagination_cursor_matrix.cpp)
cxxmcp_example(cxxmcp_pagination_cursor_matrix)

add_executable(cxxmcp_client_subscription_helper_matrix src/client_subscription_helper_matrix.cpp)
cxxmcp_example(cxxmcp_client_subscription_helper_matrix)

add_executable(cxxmcp_task_cancel_matrix src/task_cancel_matrix.cpp)
cxxmcp_example(cxxmcp_task_cancel_matrix)

add_executable(cxxmcp_transport_adapter_matrix src/transport_adapter_matrix.cpp)
cxxmcp_example(cxxmcp_transport_adapter_matrix)

if(TARGET cxxmcp::gateway OR TARGET mcp_gateway)
    add_executable(cxxmcp_runtime_services_matrix src/runtime_services_matrix.cpp)
    target_compile_features(cxxmcp_runtime_services_matrix PRIVATE cxx_std_20)
    if(TARGET cxxmcp::gateway)
        target_link_libraries(cxxmcp_runtime_services_matrix
            PRIVATE cxxmcp::gateway cxxmcp::runtime cxxmcp::sdk)
    else()
        target_link_libraries(cxxmcp_runtime_services_matrix
            PRIVATE mcp_gateway mcp_app cxxmcp::sdk)
    endif()
endif()
add_executable(cxxmcp_workspace_server src/workspace_server.cpp)
cxxmcp_example(cxxmcp_workspace_server)

add_executable(cxxmcp_log_triage_server src/log_triage_server.cpp)
cxxmcp_example(cxxmcp_log_triage_server)

add_executable(cxxmcp_sdk_smoke src/sdk_smoke.cpp)
cxxmcp_example(cxxmcp_sdk_smoke)

enable_testing()
add_test(NAME cxxmcp_sdk_smoke COMMAND cxxmcp_sdk_smoke)
add_test(NAME cxxmcp_elicitation_client COMMAND cxxmcp_elicitation_client)
add_test(NAME cxxmcp_graceful_shutdown COMMAND cxxmcp_graceful_shutdown)
add_test(NAME cxxmcp_timeout_cancellation_client
         COMMAND cxxmcp_timeout_cancellation_client)
add_test(NAME cxxmcp_client_inbound_cancellation_matrix
         COMMAND cxxmcp_client_inbound_cancellation_matrix)
add_test(NAME cxxmcp_client_callbacks_matrix COMMAND cxxmcp_client_callbacks_matrix)
add_test(NAME cxxmcp_transport_stdio_matrix COMMAND cxxmcp_transport_stdio_matrix)
add_test(NAME cxxmcp_process_stdio_client_probe COMMAND cxxmcp_process_stdio_client_probe)
add_test(NAME cxxmcp_async_request_matrix COMMAND cxxmcp_async_request_matrix)
add_test(NAME cxxmcp_policy_subscription_matrix COMMAND cxxmcp_policy_subscription_matrix)
add_test(NAME cxxmcp_extension_plugin_adapter_matrix COMMAND cxxmcp_extension_plugin_adapter_matrix)
add_test(NAME cxxmcp_server_to_client_context_matrix COMMAND cxxmcp_server_to_client_context_matrix)
add_test(NAME cxxmcp_handler_interface_matrix COMMAND cxxmcp_handler_interface_matrix)
add_test(NAME cxxmcp_native_server_transport_matrix COMMAND cxxmcp_native_server_transport_matrix)
add_test(NAME cxxmcp_rich_content_cancellation_matrix COMMAND cxxmcp_rich_content_cancellation_matrix)
add_test(NAME cxxmcp_direct_http_legacy_sse_matrix COMMAND cxxmcp_direct_http_legacy_sse_matrix)
add_test(NAME cxxmcp_http_auth_lite_matrix COMMAND cxxmcp_http_auth_lite_matrix)
add_test(NAME cxxmcp_pagination_cursor_matrix COMMAND cxxmcp_pagination_cursor_matrix)
add_test(NAME cxxmcp_client_subscription_helper_matrix COMMAND cxxmcp_client_subscription_helper_matrix)
add_test(NAME cxxmcp_task_cancel_matrix COMMAND cxxmcp_task_cancel_matrix)
add_test(NAME cxxmcp_transport_adapter_matrix COMMAND cxxmcp_transport_adapter_matrix)
if(TARGET cxxmcp_runtime_services_matrix)
    add_test(NAME cxxmcp_runtime_services_matrix COMMAND cxxmcp_runtime_services_matrix)
endif()
if(TARGET cxxmcp_http_gateway_runtime_matrix)
    add_test(NAME cxxmcp_http_gateway_runtime_matrix COMMAND cxxmcp_http_gateway_runtime_matrix)
endif()
set_tests_properties(cxxmcp_sdk_smoke PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}")
