add_executable(cxxmcp_minimal_stdio_server
    basic/minimal_stdio_server.cpp)
cxxmcp_example(cxxmcp_minimal_stdio_server)

add_executable(cxxmcp_typed_tool_server
    basic/typed_tool_server.cpp)
cxxmcp_example(cxxmcp_typed_tool_server)

add_executable(cxxmcp_streamable_http_client
    clients/streamable_http_client.cpp)
cxxmcp_example(cxxmcp_streamable_http_client)

add_executable(cxxmcp_elicitation_client
    clients/elicitation_client.cpp)
cxxmcp_example(cxxmcp_elicitation_client)
cxxmcp_register_test(cxxmcp_elicitation_client)

add_executable(cxxmcp_timeout_cancellation_client
    clients/timeout_cancellation_client.cpp)
cxxmcp_example(cxxmcp_timeout_cancellation_client)
cxxmcp_register_test(cxxmcp_timeout_cancellation_client)

add_executable(cxxmcp_graceful_shutdown
    transports/graceful_shutdown.cpp)
cxxmcp_example(cxxmcp_graceful_shutdown)
cxxmcp_register_test(cxxmcp_graceful_shutdown)

add_executable(cxxmcp_process_stdio_client_probe
    transports/process_stdio_client_probe.cpp)
cxxmcp_example(cxxmcp_process_stdio_client_probe)
target_compile_definitions(cxxmcp_process_stdio_client_probe
    PRIVATE CXXMCP_EXAMPLES_CHILD_EXE="$<TARGET_FILE:cxxmcp_minimal_stdio_server>")
add_dependencies(cxxmcp_process_stdio_client_probe cxxmcp_minimal_stdio_server)
cxxmcp_register_test(cxxmcp_process_stdio_client_probe)

add_executable(cxxmcp_workspace_server
    servers/workspace_server.cpp)
cxxmcp_example(cxxmcp_workspace_server)

add_executable(cxxmcp_git_server
    servers/git_server.cpp)
cxxmcp_example(cxxmcp_git_server)

add_executable(cxxmcp_sqlite_server
    servers/sqlite_server.cpp)
cxxmcp_example(cxxmcp_sqlite_server)

add_executable(cxxmcp_cmake_ctest_server
    servers/cmake_ctest_server.cpp)
cxxmcp_example(cxxmcp_cmake_ctest_server)

add_executable(cxxmcp_json_file_server
    servers/json_file_server.cpp)
cxxmcp_example(cxxmcp_json_file_server)

add_executable(cxxmcp_csv_server
    servers/csv_server.cpp)
cxxmcp_example(cxxmcp_csv_server)

add_executable(cxxmcp_compile_commands_server
    servers/compile_commands_server.cpp)
cxxmcp_example(cxxmcp_compile_commands_server)

add_executable(cxxmcp_jsonl_server
    servers/jsonl_server.cpp)
cxxmcp_example(cxxmcp_jsonl_server)

add_executable(cxxmcp_log_triage_server
    servers/log_triage_server.cpp)
cxxmcp_example(cxxmcp_log_triage_server)

add_executable(cxxmcp_server_to_client_context_matrix
    advanced/server_to_client_context_matrix.cpp)
cxxmcp_example(cxxmcp_server_to_client_context_matrix)
cxxmcp_register_test(cxxmcp_server_to_client_context_matrix)

add_executable(cxxmcp_native_server_transport_matrix
    advanced/native_server_transport_matrix.cpp)
cxxmcp_example(cxxmcp_native_server_transport_matrix)
cxxmcp_register_test(cxxmcp_native_server_transport_matrix)

add_executable(cxxmcp_rich_content_cancellation_matrix
    advanced/rich_content_cancellation_matrix.cpp)
cxxmcp_example(cxxmcp_rich_content_cancellation_matrix)
cxxmcp_register_test(cxxmcp_rich_content_cancellation_matrix)

add_executable(cxxmcp_direct_http_legacy_sse_matrix
    http/direct_http_legacy_sse_matrix.cpp)
cxxmcp_example(cxxmcp_direct_http_legacy_sse_matrix)

add_executable(cxxmcp_http_auth_lite_matrix
    http/http_auth_lite_matrix.cpp)
cxxmcp_example(cxxmcp_http_auth_lite_matrix)
