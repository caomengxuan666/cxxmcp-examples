# cxxmcp examples

This repository is a downstream validation suite for the C++ `cxxmcp` SDK. It
is intentionally separate from the SDK tree so it exercises the same CMake and
public-header path that application authors use.

## What This Validates

The suite is organized from small authoring examples to protocol, transport,
request-lifecycle, policy, extension, and gateway/runtime coverage.

| MCP / SDK surface | Example coverage |
| --- | --- |
| initialize / ping / initialized | `minimal_stdio_server`, `sdk_smoke`, `process_stdio_client_probe`, `http_gateway_runtime_matrix` |
| tools/list, tools/get, tools/call | `minimal_stdio_server`, `workspace_server`, `log_triage_server`, `sdk_smoke`, `async_request_matrix`, process/gateway probes |
| typed tool args/results and JSON schemas | `workspace_server`, `log_triage_server`, `extension_plugin_adapter_matrix` |
| task-backed tools and task list/get/result | `workspace_server`, `log_triage_server`, `sdk_smoke` |
| prompts/list and prompts/get | `minimal_stdio_server`, `workspace_server`, `log_triage_server`, `sdk_smoke`, process/gateway probes |
| resources/list and resources/read | `minimal_stdio_server`, `workspace_server`, `log_triage_server`, `sdk_smoke`, process/gateway probes |
| resource templates | `workspace_server`, `sdk_smoke`, `async_request_matrix` |
| resources/subscribe, resources/unsubscribe, resource updated notifications | `policy_subscription_matrix` |
| completion/complete raw and typed helper APIs | `workspace_server`, `log_triage_server`, `sdk_smoke`, `async_request_matrix` |
| sampling/createMessage server and client-side callback | `workspace_server`, `log_triage_server`, `sdk_smoke`, `client_callbacks_matrix`, `async_request_matrix` |
| elicitation/create client-side callback and schema builder | `client_callbacks_matrix` |
| outbound elicitation/create typed and async calls | `async_request_matrix` |
| roots/list and roots list-changed | `client_callbacks_matrix` |
| logging/setLevel and logging notifications | `workspace_server`, `log_triage_server`, `sdk_smoke`, `client_callbacks_matrix` |
| cancellation, progress, list-changed, elicitation-complete, task-status notifications | `client_callbacks_matrix`, `sdk_smoke`, `async_request_matrix` |
| raw/custom requests and notifications | `minimal_stdio_server`, `workspace_server`, `log_triage_server`, `sdk_smoke`, `client_callbacks_matrix` |
| RequestOptions, RequestHandle, async helpers, timeout/cancel, list_all helpers | `async_request_matrix`, `sdk_smoke` |
| role-generic transport contract | `transport_stdio_matrix` |
| child-process stdio transport, `ClientPeer::connect_stdio`, and `mcp::serve` | `process_stdio_client_probe` |
| streamable HTTP client/server via gateway runtime | `http_gateway_runtime_matrix` |
| auth provider and rate limiter hooks | `policy_subscription_matrix` |
| plugin SDK and adapter extension points | `extension_plugin_adapter_matrix` |
| runtime/gateway layer | `http_gateway_runtime_matrix` |

This is not a replacement for the SDK's unit tests. It intentionally does not
try to call every overload or every DTO serializer one by one; it covers each
public feature family with representative downstream code that must compile and
run outside the SDK repository.

## Executables

- `cxxmcp_minimal_stdio_server`: the smallest useful stdio server. It shows
  initialize, one JSON tool, one prompt, one resource, and a raw health request.
- `cxxmcp_workspace_server`: a stdio MCP server for code/workspace inspection.
  It provides bounded file reads, regex search, workspace summaries, a review
  prompt, completion suggestions, sample generation, a summary resource, a file
  URI template, and task-capable read-only tools.
- `cxxmcp_log_triage_server`: a stdio MCP server for incident/log triage. It
  summarizes severity counts, extracts matching lines, exposes a playbook
  resource, renders an incident-report prompt, provides completion suggestions,
  and supports task-capable log tools.
- `cxxmcp_client_callbacks_matrix`: a client-side matrix for server-to-client
  features. It handles `roots/list`, `sampling/createMessage`,
  `elicitation/create`, custom requests, cancellation, progress, logging, list
  changed notifications, and resource update notifications.
- `cxxmcp_transport_stdio_matrix`: validates the role-generic
  `transport::ClientStdioTransport` and `transport::ServerStdioTransport`
  message contract over caller-owned streams.
- `cxxmcp_process_stdio_client_probe`: launches `cxxmcp_minimal_stdio_server`
  as a child process through `transport::ProcessStdioClientTransport` and uses
  `mcp::serve` plus `ClientPeer` against the process, and also validates the
  `ClientPeer::connect_stdio` convenience constructor.
- `cxxmcp_async_request_matrix`: validates `RequestOptions`, request metadata,
  `RequestHandle`, async typed helpers, list-all helpers, outbound elicitation,
  timeout/cancellation behavior, and typed completion helpers.
- `cxxmcp_policy_subscription_matrix`: validates resource subscribe/unsubscribe,
  targeted resource-update notifications, server auth providers, and rate
  limiters.
- `cxxmcp_extension_plugin_adapter_matrix`: demonstrates the plugin SDK and
  adapter extension contracts by registering a plugin-backed tool into a normal
  `ToolRegistry`.
- `cxxmcp_http_gateway_runtime_matrix`: starts the runtime gateway, binds the
  minimal stdio server as an upstream, exposes it over streamable HTTP, and
  calls it with the SDK client.
- `cxxmcp_sdk_smoke`: an in-process ClientPeer/Server loopback executable that
  validates tools, prompts, resources, resource templates, completion, sampling,
  logging, raw requests, notifications, and task-backed tool calls without an
  external MCP client.

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

The adjacent-source build enables the gateway target so the HTTP/runtime example
is compiled and tested. If an installed package does not include `cxxmcp::gateway`,
the gateway example is skipped by CMake.

## Codex Config

Keep these in a repo-local config when you want to test them from Codex without
polluting the global Codex config:

```toml
[mcp_servers.cxxmcp-workspace]
command = 'C:\Users\cmx\repo\cxxmcp-examples\build\cxxmcp_workspace_server.exe'
args = ['C:\Users\cmx\repo\MCPServer.cpp']

[mcp_servers.cxxmcp-log-triage]
command = 'C:\Users\cmx\repo\cxxmcp-examples\build\cxxmcp_log_triage_server.exe'
```

## Learning Path

1. Start with `src/minimal_stdio_server.cpp` to see the compact `App::builder()`
   API and newline-delimited stdio transport.
2. Move to `src/workspace_server.cpp` and `src/log_triage_server.cpp` for typed
   arguments/results, schemas, resources, prompts, completion, sampling,
   logging, raw requests, and task support in realistic servers.
3. Read `src/client_callbacks_matrix.cpp` for client-side request and
   notification handlers that do not appear as ordinary tools.
4. Read `src/transport_stdio_matrix.cpp` and
   `src/process_stdio_client_probe.cpp` for role-generic transports and child
   process stdio.
5. Read `src/async_request_matrix.cpp` for request metadata, async helpers,
   timeout, cancellation, and typed completion helpers.
6. Read `src/policy_subscription_matrix.cpp` and
   `src/extension_plugin_adapter_matrix.cpp` for server policy hooks,
   subscriptions, plugin SDK, and adapters.
7. Read `src/http_gateway_runtime_matrix.cpp` for streamable HTTP and gateway
   runtime integration.

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

