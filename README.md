# cxxmcp examples

[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C.svg)](https://isocpp.org/)
[![CMake](https://img.shields.io/badge/build-CMake-064F8C.svg)](https://cmake.org/)
[![MCP](https://img.shields.io/badge/protocol-Model%20Context%20Protocol-111827.svg)](https://modelcontextprotocol.io/)
[![SDK](https://img.shields.io/badge/upstream-cxxmcp-0F766E.svg)](https://github.com/caomengxuan666/cxxmcp)

Application-style examples and downstream validation for the
[`cxxmcp`](https://github.com/caomengxuan666/cxxmcp) C++ MCP SDK.

This repository is intentionally separate from the SDK source tree. It consumes
`cxxmcp` like an application author would: through CMake targets, public
headers, real executables, and end-to-end probes. The examples start with small
stdio servers, then move into SDK-first `ClientPeer` / `ServerPeer` /
`Service` surfaces such as async requests, role-generic transports, direct
HTTP/SSE, task cancellation, plugin adapters, gateway runtime, and app service
management.

## Repository Role

Use this repository when you want to:

- Learn the SDK from small examples through advanced integration patterns.
- Validate that an installed or adjacent `cxxmcp` SDK can be consumed by a
  downstream CMake project.
- Exercise realistic MCP client/server behavior beyond isolated unit tests.
- Smoke-test gateway/runtime and app-service APIs outside the SDK repository.

## What This Validates

The suite is organized from small authoring examples to protocol, transport,
request-lifecycle, policy, extension, gateway/runtime, and app-service coverage.

| MCP / SDK surface | Example coverage |
| --- | --- |
| initialize / ping / initialized | `minimal_stdio_server`, `sdk_smoke`, `process_stdio_client_probe`, `http_gateway_runtime_matrix` |
| tools/list, tools/get, tools/call | `minimal_stdio_server`, `workspace_server`, `log_triage_server`, `sdk_smoke`, `async_request_matrix`, process/gateway probes |
| typed tool args/results and JSON schemas | `typed_tool_server`, `workspace_server`, `log_triage_server`, `extension_plugin_adapter_matrix` |
| task-backed tools and task list/get/result/cancel | `workspace_server`, `log_triage_server`, `sdk_smoke`, `task_cancel_matrix`, `server_to_client_context_matrix` |
| prompts/list and prompts/get | `minimal_stdio_server`, `workspace_server`, `log_triage_server`, `sdk_smoke`, process/gateway probes |
| resources/list and resources/read | `minimal_stdio_server`, `workspace_server`, `log_triage_server`, `sdk_smoke`, process/gateway probes |
| resource templates | `workspace_server`, `sdk_smoke`, `async_request_matrix` |
| resources/subscribe, resources/unsubscribe, resource updated notifications | `policy_subscription_matrix`, `client_subscription_helper_matrix` |
| completion/complete raw and typed helper APIs | `workspace_server`, `log_triage_server`, `sdk_smoke`, `async_request_matrix` |
| sampling/createMessage server and client-side callback | `workspace_server`, `log_triage_server`, `sdk_smoke`, `client_callbacks_matrix`, `async_request_matrix` |
| elicitation/create client-side callback and schema builder | `client_callbacks_matrix` |
| outbound elicitation/create typed and async calls | `elicitation_client`, `async_request_matrix` |
| roots/list and roots list-changed | `client_callbacks_matrix` |
| logging/setLevel and logging notifications | `workspace_server`, `log_triage_server`, `sdk_smoke`, `client_callbacks_matrix` |
| cancellation, progress, list-changed, elicitation-complete, task-status notifications | `client_callbacks_matrix`, `sdk_smoke`, `async_request_matrix` |
| raw/custom requests and notifications | `minimal_stdio_server`, `workspace_server`, `log_triage_server`, `sdk_smoke`, `client_callbacks_matrix` |
| RequestOptions, RequestHandle, async helpers, timeout/cancel, list_all helpers and cursor pagination | `async_request_matrix`, `pagination_cursor_matrix`, `sdk_smoke` |
| role-generic transport contract | `transport_stdio_matrix`, `transport_adapter_matrix` |
| child-process stdio transport, `ClientPeer::connect_stdio`, and `mcp::serve` | `process_stdio_client_probe` |
| standalone streamable HTTP client | `streamable_http_client` |
| streamable HTTP client/server via gateway runtime | `http_gateway_runtime_matrix` |
| direct streamable HTTP server/client and legacy SSE client path | `direct_http_legacy_sse_matrix` |
| auth provider and rate limiter hooks | `policy_subscription_matrix` |
| plugin SDK and adapter extension points | `extension_plugin_adapter_matrix` |
| client/server legacy transport adapters and role-generic contract adapters | `transport_adapter_matrix` |
| runtime/gateway layer | `http_gateway_runtime_matrix`, `runtime_services_matrix` |
| server handler uses `ToolContext::client()` / `SessionContext::client()` | `server_to_client_context_matrix` |
| `ClientHandler` / `ClientHandlerInterface` and `ServerHandler` / `ServerHandlerInterface` | `handler_interface_matrix` |
| graceful service shutdown and cancellation | `graceful_shutdown`, `process_stdio_client_probe`, `direct_http_legacy_sse_matrix` |
| custom role-generic `transport::ServerTransport` with `ServerPeer::serve_transport` | `native_server_transport_matrix` |
| rich content blocks: image, audio, embedded resource, resource link, annotations, `_meta` | `rich_content_cancellation_matrix` |
| server-side cooperative cancellation through `ToolContext::cancelled()` | `rich_content_cancellation_matrix`, `task_cancel_matrix` |
| app service layer: memory/json stores, import/export, config, readiness/status, onboarding, exposure/server management | `runtime_services_matrix` |

This is not a replacement for the SDK's unit tests. It intentionally does not
try to call every overload or every DTO serializer one by one; it covers each
public feature family with representative downstream code that must compile and
run outside the SDK repository.

## Executables

- `cxxmcp_minimal_stdio_server`: the smallest useful stdio server. It shows
  initialize, one JSON tool, one prompt, one resource, and a raw health request.
- `cxxmcp_typed_tool_server`: a compact typed stdio server. It shows
  `from_json`, `to_json`, `SchemaTraits`, typed tool registration, output
  schema, and `ToolContext` access.
- `cxxmcp_streamable_http_client`: a standalone `ClientPeer` + `Service`
  streamable HTTP client. Pass an endpoint URI or use the default
  `http://127.0.0.1:3000/mcp`.
- `cxxmcp_elicitation_client`: a client-side elicitation handler example that
  accepts or declines `elicitation/create` requests.
- `cxxmcp_graceful_shutdown`: a small `ServerPeer` + `Service` lifecycle probe
  showing cooperative cancellation and idempotent shutdown.
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
- `cxxmcp_server_to_client_context_matrix`: invokes client roots, sampling,
  elicitation, task listing, and elicitation-complete notification from inside
  a server tool through `ToolContext::client()`.
- `cxxmcp_handler_interface_matrix`: validates aggregate/interface-style
  `ClientHandler` and `ServerHandler` installation paths.
- `cxxmcp_native_server_transport_matrix`: drives `ServerPeer::serve_transport`
  with a custom role-generic `transport::ServerTransport`.
- `cxxmcp_rich_content_cancellation_matrix`: demonstrates image/audio/resource
  content blocks, annotations, `_meta`, output schema, and cooperative
  cancellation observed through `ToolContext::cancelled()`.
- `cxxmcp_direct_http_legacy_sse_matrix`: runs the direct HTTP transport path
  without the gateway wrapper and verifies both streamable HTTP and legacy SSE
  client connection helpers.
- `cxxmcp_pagination_cursor_matrix`: drives cursor-based list pagination for
  tools, prompts, resources, resource templates, and tasks through the
  `ClientPeer::list_all_*` helpers.
- `cxxmcp_client_subscription_helper_matrix`: uses `ClientPeer::subscribe()` and
  `unsubscribe()` plus targeted resource update notifications in a loopback
  client/server pair.
- `cxxmcp_task_cancel_matrix`: starts a task-backed tool, cancels it through the
  SDK task API, and observes the cancelled task state.
- `cxxmcp_transport_adapter_matrix`: covers client and server adapters in both
  directions: legacy `client::Transport` / `server::Transport` to the
  role-generic contract, and contract transports back to concrete client/server
  APIs, including handler and failure paths.
- `cxxmcp_http_gateway_runtime_matrix`: starts the runtime gateway, binds the
  minimal stdio server as an upstream, exposes it over streamable HTTP, and
  calls it with the SDK client.
- `cxxmcp_runtime_services_matrix`: exercises the application/runtime service
  layer: in-memory stores, JSON-backed stores, bundle import/export, client
  config generation, readiness/status checks, onboarding, config import,
  server discovery, and exposure profile management.
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

The adjacent-source build enables the gateway target so the HTTP/runtime and
app service examples are compiled and tested. If an installed package does not
include `cxxmcp::gateway`, the gateway examples are skipped by CMake.

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

1. Start with `src/minimal_stdio_server.cpp` to see the compact stdio server
   shape and newline-delimited transport.
2. Read `src/typed_tool_server.cpp` for the typed tool path: JSON schema,
   `from_json`, `to_json`, and `ToolContext`.
3. Read `src/streamable_http_client.cpp` and
   `src/process_stdio_client_probe.cpp` for `ClientPeer` plus `Service` over
   network and child-process transports.
4. Move to `src/workspace_server.cpp` and `src/log_triage_server.cpp` for typed
   arguments/results, schemas, resources, prompts, completion, sampling,
   logging, raw requests, and task support in realistic servers.
5. Read `src/client_callbacks_matrix.cpp` and `src/elicitation_client.cpp` for
   client-side request and notification handlers that do not appear as ordinary
   tools.
6. Read `src/graceful_shutdown.cpp`, `src/transport_stdio_matrix.cpp`, and
   `src/native_server_transport_matrix.cpp` for service shutdown,
   role-generic transports, and custom server transports.
7. Read `src/async_request_matrix.cpp` for request metadata, async helpers,
   timeout, cancellation, and typed completion helpers.
8. Read `src/policy_subscription_matrix.cpp` and
   `src/extension_plugin_adapter_matrix.cpp` for server policy hooks,
   subscriptions, plugin SDK, and adapters.
9. Read `src/server_to_client_context_matrix.cpp`,
   `src/handler_interface_matrix.cpp`,
   `src/native_server_transport_matrix.cpp`, and
   `src/rich_content_cancellation_matrix.cpp` for advanced peer callbacks,
   handler contracts, custom transports, rich content, and cancellation.
10. Read `src/direct_http_legacy_sse_matrix.cpp`,
   `src/pagination_cursor_matrix.cpp`,
   `src/client_subscription_helper_matrix.cpp`, and
   `src/task_cancel_matrix.cpp` for direct HTTP/SSE, pagination,
   subscriptions, and task cancellation.
11. Read `src/transport_adapter_matrix.cpp` for concrete-to-contract transport
   bridges on both client and server roles.
12. Read `src/http_gateway_runtime_matrix.cpp` and
    `src/runtime_services_matrix.cpp` for streamable HTTP, gateway runtime,
    persisted app stores, client config, readiness/status, onboarding, and
    exposure management.

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


