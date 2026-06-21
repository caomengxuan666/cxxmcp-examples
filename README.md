# cxxmcp examples

[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C.svg)](https://isocpp.org/)
[![CMake](https://img.shields.io/badge/build-CMake-064F8C.svg)](https://cmake.org/)
[![MCP](https://img.shields.io/badge/protocol-Model%20Context%20Protocol-111827.svg)](https://modelcontextprotocol.io/)
[![SDK](https://img.shields.io/badge/upstream-cxxmcp-0F766E.svg)](https://github.com/caomengxuan666/cxxmcp)

Copyable application-style examples and downstream validation for the
[`cxxmcp`](https://github.com/caomengxuan666/cxxmcp) C++ MCP SDK.

This repository is intentionally separate from the SDK source tree. It consumes
`cxxmcp` like an application author would: through CMake targets, public
headers, real executables, and end-to-end probes.

## Repository Role

Use this repository when you want to:

- Copy a small, working C++ MCP program and adapt it into an application.
- Learn the SDK from stdio servers through HTTP clients, task cancellation,
  typed tools, subscriptions, callbacks, and transport adapters.
- Validate that an installed or adjacent `cxxmcp` SDK can be consumed by a
  downstream CMake project.
- Exercise realistic MCP client/server behavior beyond isolated SDK unit tests.

## Examples First

The `examples/` tree is the main entry point. It contains copyable programs;
each one builds as an executable with a `cxxmcp_` target name and is kept small
enough to read as downstream application code.

The `validation/` tree has a different job: downstream probes, matrices, smoke
tests, and conformance harnesses that prove SDK consumption and behavior across
public feature families. See [`docs/validation.md`](docs/validation.md) for the
full validation map.

Start here:

- `examples/basic/minimal_stdio_server.cpp`: smallest useful stdio server with
  initialize, one tool, one prompt, one resource, and a raw health request.
- `examples/basic/typed_tool_server.cpp`: compact typed tool object with
  reflected argument/result structs and `ToolContext` access.
- `examples/servers/workspace_server.cpp`: realistic read-only workspace server
  with tools, prompts, resources, templates, completion, sampling, logging, and
  task-capable operations.
- `examples/servers/git_server.cpp`: bounded read-only Git status, log, and diff
  tools.
- `examples/servers/sqlite_server.cpp`: bounded read-only SQLite schema, table,
  and query tools backed by the `sqlite3` command when it is available.
- `examples/servers/cmake_ctest_server.cpp`: CMake preset and CTest inspection
  tools, with test execution gated by an explicit `allow_run` argument.
- `examples/servers/json_file_server.cpp`: bounded JSON summary and JSON
  pointer lookup tools.
- `examples/servers/csv_server.cpp`: bounded CSV summary and sample tools.
- `examples/servers/compile_commands_server.cpp`: bounded
  `compile_commands.json` summary and source-command lookup tools.
- `examples/servers/jsonl_server.cpp`: bounded JSON Lines summary and sample
  tools.
- `examples/servers/log_triage_server.cpp`: incident/log triage server with
  typed tools, prompts, resources, completion, and task-capable log analysis.
- `examples/clients/streamable_http_client.cpp`: standalone `ClientPeer` +
  `Service` streamable HTTP client.
- `examples/clients/elicitation_client.cpp`: client-side `elicitation/create`
  handler.
- `examples/transports/graceful_shutdown.cpp`: cooperative cancellation and
  idempotent `ServerPeer` + `Service` shutdown.
- `examples/clients/timeout_cancellation_client.cpp`: request timeouts,
  cancellation tokens, cancellation notifications, and idempotent
  `RequestHandle::cancel()`.

## Executables

### Copyable Examples

- `cxxmcp_minimal_stdio_server`: the smallest useful stdio server. It shows
  initialize, one JSON tool, one prompt, one resource, and a raw health request.
- `cxxmcp_typed_tool_server`: a compact typed stdio server. It shows a tool
  object, `CXXMCP_REFLECT_SELF`, generated input/output schemas, and
  `ToolContext` access.
- `cxxmcp_streamable_http_client`: a standalone `ClientPeer` + `Service`
  streamable HTTP client. Pass an endpoint URI or use the default
  `http://127.0.0.1:3000/mcp`.
- `cxxmcp_elicitation_client`: a client-side elicitation handler example that
  accepts or declines `elicitation/create` requests.
- `cxxmcp_graceful_shutdown`: a small `ServerPeer` + `Service` lifecycle probe
  showing cooperative cancellation and idempotent shutdown.
- `cxxmcp_timeout_cancellation_client`: a focused `ClientPeer` example showing
  request timeouts, external cancellation tokens, cancellation notifications,
  and idempotent `RequestHandle::cancel()`.
- `cxxmcp_workspace_server`: a stdio MCP server for code/workspace inspection.
  It provides bounded file reads, regex search, workspace summaries, a review
  prompt, completion suggestions, sample generation, a summary resource, a file
  URI template, and task-capable read-only tools.
- `cxxmcp_git_server`: a stdio MCP server for bounded read-only Git inspection.
  It exposes status, log, and diff tools with structured output.
- `cxxmcp_sqlite_server`: a stdio MCP server for bounded read-only SQLite
  inspection. It uses the `sqlite3` command at runtime and reports a normal tool
  error when that command is unavailable.
- `cxxmcp_cmake_ctest_server`: a stdio MCP server for CMake/CTest inspection.
  It reads `CMakePresets.json`, lists tests from a build directory, and runs
  selected CTest tests only when `allow_run=true`.
- `cxxmcp_json_file_server`: a stdio MCP server for bounded JSON file
  inspection. It summarizes root structure and reads values by JSON pointer.
- `cxxmcp_csv_server`: a stdio MCP server for bounded CSV inspection. It
  summarizes columns and returns small row samples.
- `cxxmcp_compile_commands_server`: a stdio MCP server for bounded
  `compile_commands.json` inspection. It summarizes translation units and finds
  compile commands by source path text.
- `cxxmcp_jsonl_server`: a stdio MCP server for bounded JSON Lines inspection.
  It summarizes line validity, object keys, and small row samples.
- `cxxmcp_log_triage_server`: a stdio MCP server for incident/log triage. It
  summarizes severity counts, extracts matching lines, exposes a playbook
  resource, renders an incident-report prompt, provides completion suggestions,
  and supports task-capable log tools.

### Validation And Conformance

- `cxxmcp_sdk_smoke`: in-process `ClientPeer`/`ServerPeer` loopback coverage
  for tools, prompts, resources, templates, completion, sampling, logging, raw
  requests, notifications, and task-backed tool calls.
- `cxxmcp_process_stdio_client_probe`: launches the minimal server as a child
  process and validates process stdio client consumption.
- `cxxmcp_conformance_everything_server`: streamable HTTP server for
  `modelcontextprotocol/conformance` server-mode testing.
- `cxxmcp_conformance_everything_client`: command-style client harness for
  `modelcontextprotocol/conformance` client-mode testing.
- Matrix targets cover callbacks, cancellation, async requests, policy hooks,
  subscriptions, pagination, task cancellation, direct HTTP/SSE, HTTP auth-lite,
  transport adapters, native server transports, handler interfaces, rich
  content, gateway runtime, and runtime services.

The validation targets intentionally do not call every overload or every DTO
serializer one by one. They cover each public feature family with
representative downstream code that must compile and run outside the SDK
repository.

## Build

From this directory:

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

By default the build consumes the adjacent SDK source tree at
`../MCPServer.cpp`. To validate an installed SDK package instead:

```powershell
cmake -S . -B build-installed -DCXXMCP_EXAMPLES_USE_ADJACENT_SDK=OFF -DCMAKE_PREFIX_PATH=<install-prefix>
cmake --build build-installed --config Release
```

The adjacent-source build enables the gateway target so the HTTP/runtime and
app service examples are compiled and tested. If an installed package does not
include `cxxmcp::gateway`, the gateway examples are skipped by CMake.

CI runs both modes. The installed-package job first installs the SDK into a
temporary prefix and then configures this repository with
`CXXMCP_EXAMPLES_USE_ADJACENT_SDK=OFF`, so it proves normal
`find_package(cxxmcp CONFIG REQUIRED)` consumption instead of relying on
`add_subdirectory`.

For a focused check while developing against a local SDK checkout:

```powershell
cmake -S . -B build -DCXXMCP_EXAMPLES_SDK_SOURCE_DIR=C:/Users/cmx/repo/MCPServer.cpp
cmake --build build --config Release --target cxxmcp_http_auth_lite_matrix cxxmcp_client_inbound_cancellation_matrix cxxmcp_task_cancel_matrix
ctest --test-dir build -C Release -R "cxxmcp_(http_auth_lite_matrix|client_inbound_cancellation_matrix|task_cancel_matrix)" --output-on-failure
```

`cxxmcp_http_auth_lite_matrix` is the smallest copyable HTTP auth-lite flow:
the server installs `AuthProvider`, `HttpTransportOptions::auth_challenge`, and
`ServerPeer`/`mcp::serve`; the client uses
`Client::StreamableHttpEndpoint::auth_header = "valid-token"` through
`ClientPeer::connect_streamable_http`, which the SDK sends as
`Authorization: Bearer valid-token`.

## MCP Conformance

The conformance harness is maintained separately at
`modelcontextprotocol/conformance`. The short server-mode flow is:

```powershell
cmake --build build --target cxxmcp_conformance_everything_server --config Release
.\build\cxxmcp_conformance_everything_server.exe 3000
```

Then run the active server suite from a conformance checkout:

```powershell
npm start -- server --url http://127.0.0.1:3000/mcp --suite active --verbose
```

Current active-suite validation is 40 passing checks and 0 failures. The latest
saved all-suite server run is 108 passing checks and 1 compatibility-mode
failure, and the client all-suite run is 428 passing checks and 8 failing
checks. See [`docs/validation.md`](docs/validation.md) and
[`CONFORMANCE_STATUS.md`](CONFORMANCE_STATUS.md) for exact commands, known
exceptions, and raw stderr notes.

## Codex Config

Keep these in a repo-local config when you want to test them from Codex without
polluting the global Codex config:

```toml
[mcp_servers.cxxmcp-workspace]
command = 'C:\Users\cmx\repo\cxxmcp-examples\build\cxxmcp_workspace_server.exe'
args = ['C:\Users\cmx\repo\MCPServer.cpp']

[mcp_servers.cxxmcp-log-triage]
command = 'C:\Users\cmx\repo\cxxmcp-examples\build\cxxmcp_log_triage_server.exe'

[mcp_servers.cxxmcp-git]
command = 'C:\Users\cmx\repo\cxxmcp-examples\build\cxxmcp_git_server.exe'

[mcp_servers.cxxmcp-sqlite]
command = 'C:\Users\cmx\repo\cxxmcp-examples\build\cxxmcp_sqlite_server.exe'

[mcp_servers.cxxmcp-cmake-ctest]
command = 'C:\Users\cmx\repo\cxxmcp-examples\build\cxxmcp_cmake_ctest_server.exe'

[mcp_servers.cxxmcp-json-file]
command = 'C:\Users\cmx\repo\cxxmcp-examples\build\cxxmcp_json_file_server.exe'

[mcp_servers.cxxmcp-csv]
command = 'C:\Users\cmx\repo\cxxmcp-examples\build\cxxmcp_csv_server.exe'

[mcp_servers.cxxmcp-compile-commands]
command = 'C:\Users\cmx\repo\cxxmcp-examples\build\cxxmcp_compile_commands_server.exe'

[mcp_servers.cxxmcp-jsonl]
command = 'C:\Users\cmx\repo\cxxmcp-examples\build\cxxmcp_jsonl_server.exe'
```

## Learning Path

1. Start with `examples/basic/minimal_stdio_server.cpp` to see the compact
   stdio server shape and newline-delimited transport.
2. Read `examples/basic/typed_tool_server.cpp` for the typed tool path:
   reflected DTOs, generated schemas, tool-object authoring, and `ToolContext`.
3. Read `examples/clients/streamable_http_client.cpp` and
   `examples/transports/process_stdio_client_probe.cpp` for `ClientPeer` plus
   `Service` over network and child-process transports.
4. Move to `examples/servers/workspace_server.cpp`,
   `examples/servers/git_server.cpp`, `examples/servers/sqlite_server.cpp`,
   `examples/servers/cmake_ctest_server.cpp`, and
   `examples/servers/log_triage_server.cpp` for typed arguments/results,
   schemas, resources, prompts, completion, sampling, logging, raw requests,
   task support, and bounded native tool inspection in realistic servers. Read
   `examples/servers/json_file_server.cpp`, `examples/servers/csv_server.cpp`,
   `examples/servers/compile_commands_server.cpp`, and
   `examples/servers/jsonl_server.cpp` for compact pure-C++ file-inspection
   servers.
5. Read `validation/matrices/client_callbacks_matrix.cpp`,
   `validation/matrices/client_inbound_cancellation_matrix.cpp`, and
   `examples/clients/elicitation_client.cpp` for client-side request and
   notification handlers that do not appear as ordinary tools, including
   cancellation-aware inbound callbacks.
6. Read `examples/transports/graceful_shutdown.cpp`,
   `validation/matrices/transport_stdio_matrix.cpp`, and
   `examples/advanced/native_server_transport_matrix.cpp` for service shutdown,
   role-generic transports, and custom server transports.
7. Read `examples/clients/timeout_cancellation_client.cpp` first for focused
   timeout and cancellation behavior, then
   `validation/matrices/async_request_matrix.cpp` for request metadata, async
   helpers, list-all helpers, and typed completion helpers.
8. Read `validation/matrices/policy_subscription_matrix.cpp` and
   `validation/matrices/extension_plugin_adapter_matrix.cpp` for server policy
   hooks, subscriptions, plugin SDK, and adapters.
9. Read `examples/advanced/server_to_client_context_matrix.cpp`,
   `validation/matrices/handler_interface_matrix.cpp`,
   `examples/advanced/native_server_transport_matrix.cpp`, and
   `examples/advanced/rich_content_cancellation_matrix.cpp` for advanced peer
   callbacks, handler contracts, custom transports, rich content, and
   cancellation.
10. Read `examples/http/direct_http_legacy_sse_matrix.cpp`,
    `examples/http/http_auth_lite_matrix.cpp`,
    `validation/matrices/pagination_cursor_matrix.cpp`,
    `validation/matrices/client_subscription_helper_matrix.cpp`, and
    `validation/matrices/task_cancel_matrix.cpp` for direct HTTP/SSE, HTTP
    auth-lite, pagination, subscriptions, and task cancellation.
11. Read `validation/matrices/transport_adapter_matrix.cpp` for
    concrete-to-contract transport bridges on both client and server roles.
12. Read `validation/matrices/http_gateway_runtime_matrix.cpp` and
    `validation/matrices/runtime_services_matrix.cpp` for streamable HTTP,
    gateway runtime, persisted app stores, client config, readiness/status,
    onboarding, and exposure management.

## Manual JSON-RPC Probes

The stdio transport is newline-delimited JSON-RPC. After sending `initialize`,
you can exercise non-tool MCP methods directly.

List prompts:

```json
{"jsonrpc":"2.0","id":2,"method":"prompts/list","params":{}}
```

Render a prompt:

```json
{"jsonrpc":"2.0","id":3,"method":"prompts/get","params":{"name":"workspace.review","arguments":{"goal":"check release readiness"}}}
```

Read a resource:

```json
{"jsonrpc":"2.0","id":4,"method":"resources/read","params":{"uri":"workspace://summary"}}
```

List resource templates:

```json
{"jsonrpc":"2.0","id":5,"method":"resources/templates/list","params":{}}
```

Request completions:

```json
{"jsonrpc":"2.0","id":6,"method":"completion/complete","params":{"prefix":"workspace."}}
```

Request server-side sampling:

```json
{"jsonrpc":"2.0","id":7,"method":"sampling/createMessage","params":{"prompt":"summarize the workspace"}}
```

Change logging level:

```json
{"jsonrpc":"2.0","id":8,"method":"logging/setLevel","params":{"level":"debug"}}
```

Call a tool as a task:

```json
{"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"workspace.scan","arguments":{"max_files":100},"task":{"ttl":60}}}
```

Call a read-only Git tool:

```json
{"jsonrpc":"2.0","id":10,"method":"tools/call","params":{"name":"git.status","arguments":{"repo":"C:/Users/cmx/repo/MCPServer.cpp","max_bytes":65536}}}
```

Call a read-only SQLite query:

```json
{"jsonrpc":"2.0","id":11,"method":"tools/call","params":{"name":"sqlite.query","arguments":{"database":"C:/path/to/app.db","sql":"SELECT name FROM sqlite_master WHERE type = 'table'","max_rows":50}}}
```

List CTest tests:

```json
{"jsonrpc":"2.0","id":12,"method":"tools/call","params":{"name":"ctest.list","arguments":{"build_dir":"C:/Users/cmx/repo/cxxmcp-examples/build-reorg-check"}}}
```

Read a JSON pointer:

```json
{"jsonrpc":"2.0","id":13,"method":"tools/call","params":{"name":"json.pointer","arguments":{"path":"C:/Users/cmx/repo/cxxmcp-examples/docs/validation.md","pointer":""}}}
```

Sample a CSV file:

```json
{"jsonrpc":"2.0","id":14,"method":"tools/call","params":{"name":"csv.sample","arguments":{"path":"C:/path/to/data.csv","rows":10}}}
```

Find compile commands for a source file:

```json
{"jsonrpc":"2.0","id":15,"method":"tools/call","params":{"name":"compile_commands.find","arguments":{"path":"C:/Users/cmx/repo/cxxmcp-examples/build-reorg-check/compile_commands.json","file_contains":"workspace_server.cpp"}}}
```

Summarize JSON Lines:

```json
{"jsonrpc":"2.0","id":16,"method":"tools/call","params":{"name":"jsonl.summary","arguments":{"path":"C:/path/to/events.jsonl","max_lines":1000}}}
```

## Run with an MCP client

Workspace server:

```json
{
  "mcpServers": {
    "cxxmcp-workspace": {
      "command": "C:/Users/cmx/repo/cxxmcp-examples/build/Release/cxxmcp_workspace_server.exe",
      "args": ["C:/Users/cmx/repo/MCPServer.cpp"]
    }
  }
}
```

Log triage server:

```json
{
  "mcpServers": {
    "cxxmcp-log-triage": {
      "command": "C:/Users/cmx/repo/cxxmcp-examples/build/Release/cxxmcp_log_triage_server.exe"
    }
  }
}
```

Git server:

```json
{
  "mcpServers": {
    "cxxmcp-git": {
      "command": "C:/Users/cmx/repo/cxxmcp-examples/build/Release/cxxmcp_git_server.exe"
    }
  }
}
```

SQLite server:

```json
{
  "mcpServers": {
    "cxxmcp-sqlite": {
      "command": "C:/Users/cmx/repo/cxxmcp-examples/build/Release/cxxmcp_sqlite_server.exe"
    }
  }
}
```

CMake/CTest server:

```json
{
  "mcpServers": {
    "cxxmcp-cmake-ctest": {
      "command": "C:/Users/cmx/repo/cxxmcp-examples/build/Release/cxxmcp_cmake_ctest_server.exe"
    }
  }
}
```

JSON file server:

```json
{
  "mcpServers": {
    "cxxmcp-json-file": {
      "command": "C:/Users/cmx/repo/cxxmcp-examples/build/Release/cxxmcp_json_file_server.exe"
    }
  }
}
```

CSV server:

```json
{
  "mcpServers": {
    "cxxmcp-csv": {
      "command": "C:/Users/cmx/repo/cxxmcp-examples/build/Release/cxxmcp_csv_server.exe"
    }
  }
}
```

Compile commands server:

```json
{
  "mcpServers": {
    "cxxmcp-compile-commands": {
      "command": "C:/Users/cmx/repo/cxxmcp-examples/build/Release/cxxmcp_compile_commands_server.exe"
    }
  }
}
```

JSON Lines server:

```json
{
  "mcpServers": {
    "cxxmcp-jsonl": {
      "command": "C:/Users/cmx/repo/cxxmcp-examples/build/Release/cxxmcp_jsonl_server.exe"
    }
  }
}
```
