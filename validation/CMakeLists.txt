add_executable(cxxmcp_sdk_smoke
    smoke/sdk_smoke.cpp)
cxxmcp_example(cxxmcp_sdk_smoke)
cxxmcp_register_test(cxxmcp_sdk_smoke)
set_tests_properties(cxxmcp_sdk_smoke PROPERTIES
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

add_executable(cxxmcp_client_inbound_cancellation_matrix
    matrices/client_inbound_cancellation_matrix.cpp)
cxxmcp_example(cxxmcp_client_inbound_cancellation_matrix)
cxxmcp_register_test(cxxmcp_client_inbound_cancellation_matrix)

add_executable(cxxmcp_client_callbacks_matrix
    matrices/client_callbacks_matrix.cpp)
cxxmcp_example(cxxmcp_client_callbacks_matrix)
cxxmcp_register_test(cxxmcp_client_callbacks_matrix)

add_executable(cxxmcp_transport_stdio_matrix
    matrices/transport_stdio_matrix.cpp)
cxxmcp_example(cxxmcp_transport_stdio_matrix)
cxxmcp_register_test(cxxmcp_transport_stdio_matrix)

add_executable(cxxmcp_async_request_matrix
    matrices/async_request_matrix.cpp)
cxxmcp_example(cxxmcp_async_request_matrix)
cxxmcp_register_test(cxxmcp_async_request_matrix)

add_executable(cxxmcp_policy_subscription_matrix
    matrices/policy_subscription_matrix.cpp)
cxxmcp_example(cxxmcp_policy_subscription_matrix)
cxxmcp_register_test(cxxmcp_policy_subscription_matrix)

if(TARGET cxxmcp::plugin_sdk AND TARGET cxxmcp::adapters)
    add_executable(cxxmcp_extension_plugin_adapter_matrix
        matrices/extension_plugin_adapter_matrix.cpp)
    target_compile_features(cxxmcp_extension_plugin_adapter_matrix
        PRIVATE cxx_std_20)
    target_link_libraries(cxxmcp_extension_plugin_adapter_matrix
        PRIVATE cxxmcp::sdk cxxmcp::plugin_sdk cxxmcp::adapters)
    cxxmcp_register_test(cxxmcp_extension_plugin_adapter_matrix)
endif()

add_executable(cxxmcp_handler_interface_matrix
    matrices/handler_interface_matrix.cpp)
cxxmcp_example(cxxmcp_handler_interface_matrix)
cxxmcp_register_test(cxxmcp_handler_interface_matrix)

add_executable(cxxmcp_pagination_cursor_matrix
    matrices/pagination_cursor_matrix.cpp)
cxxmcp_example(cxxmcp_pagination_cursor_matrix)
cxxmcp_register_test(cxxmcp_pagination_cursor_matrix)

add_executable(cxxmcp_client_subscription_helper_matrix
    matrices/client_subscription_helper_matrix.cpp)
cxxmcp_example(cxxmcp_client_subscription_helper_matrix)
cxxmcp_register_test(cxxmcp_client_subscription_helper_matrix)

add_executable(cxxmcp_task_cancel_matrix
    matrices/task_cancel_matrix.cpp)
cxxmcp_example(cxxmcp_task_cancel_matrix)
cxxmcp_register_test(cxxmcp_task_cancel_matrix)

add_executable(cxxmcp_transport_adapter_matrix
    matrices/transport_adapter_matrix.cpp)
cxxmcp_example(cxxmcp_transport_adapter_matrix)
cxxmcp_register_test(cxxmcp_transport_adapter_matrix)

if(TARGET cxxmcp::gateway OR TARGET mcp_gateway)
    add_executable(cxxmcp_http_gateway_runtime_matrix
        matrices/http_gateway_runtime_matrix.cpp)
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
    cxxmcp_register_test(cxxmcp_http_gateway_runtime_matrix)

    add_executable(cxxmcp_runtime_services_matrix
        matrices/runtime_services_matrix.cpp)
    target_compile_features(cxxmcp_runtime_services_matrix PRIVATE cxx_std_20)
    if(TARGET cxxmcp::gateway)
        target_link_libraries(cxxmcp_runtime_services_matrix
            PRIVATE cxxmcp::gateway cxxmcp::runtime cxxmcp::sdk)
    else()
        target_link_libraries(cxxmcp_runtime_services_matrix
            PRIVATE mcp_gateway mcp_app cxxmcp::sdk)
    endif()
    cxxmcp_register_test(cxxmcp_runtime_services_matrix)
endif()

add_executable(cxxmcp_conformance_everything_server
    conformance/conformance_everything_server.cpp)
cxxmcp_example(cxxmcp_conformance_everything_server)

add_executable(cxxmcp_conformance_typed_authoring_server
    conformance/conformance_typed_authoring_server.cpp)
cxxmcp_example(cxxmcp_conformance_typed_authoring_server)

if(CXXMCP_ENABLE_INSPECTOR_SMOKE)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME cxxmcp_inspector_typed_authoring_smoke
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_inspector_smoke.py"
            --server-exe "$<TARGET_FILE:cxxmcp_conformance_typed_authoring_server>")
    set_tests_properties(cxxmcp_inspector_typed_authoring_smoke PROPERTIES
        LABELS "inspector;upstream;typed-api"
        TIMEOUT 90)
endif()

add_executable(cxxmcp_conformance_everything_client
    conformance/conformance_everything_client.cpp)
cxxmcp_example(cxxmcp_conformance_everything_client)
if(TARGET cxxmcp::auth)
    target_link_libraries(cxxmcp_conformance_everything_client
        PRIVATE cxxmcp::auth)
    target_compile_definitions(cxxmcp_conformance_everything_client
        PRIVATE CXXMCP_EXAMPLES_ENABLE_AUTH=1)
endif()
if(TARGET cxxmcp::auth_openssl)
    target_link_libraries(cxxmcp_conformance_everything_client
        PRIVATE cxxmcp::auth_openssl)
    target_compile_definitions(cxxmcp_conformance_everything_client
        PRIVATE CXXMCP_EXAMPLES_ENABLE_AUTH_OPENSSL=1)
endif()

# The client harness includes the bundled httplib header directly (needed by
# the auth scenarios). httplib is header-only, so every TU that includes it
# must be compiled with the same CPPHTTPLIB_* definitions as the SDK targets,
# otherwise two different ClientImpl layouts end up linked into the same
# binary (ODR violation) and member offsets diverge between the weak copies.
# Linking the sdk's cpp_httplib interface target inherits both the include
# directory and the compile definitions, keeping the TU in sync.
if(TARGET cpp_httplib)
    target_link_libraries(cxxmcp_conformance_everything_client
        PRIVATE cpp_httplib)
endif()
