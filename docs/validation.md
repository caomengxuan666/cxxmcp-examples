# Validation

This repository has two related surfaces:

- `examples/`: copyable programs that show how an application can
  consume the `cxxmcp` SDK.
- `validation/`: downstream probes, matrices, package-consumption checks, and
  conformance harnesses that exercise SDK behavior outside the SDK repository.

The validation suite is not a replacement for the SDK's unit tests. It
intentionally does not try to call every overload or every DTO serializer one by
one; it covers each public feature family with representative downstream code
that must compile and run outside the SDK repository.

## Coverage Matrix

| MCP / SDK surface | Example coverage |
| --- | --- |
| initialize / ping / initialized | `minimal_stdio_server`, `sdk_smoke`, `process_stdio_client_probe`, `http_gateway_runtime_matrix` |
| tools/list, tools/get, tools/call | `minimal_stdio_server`, `workspace_server`, `git_server`, `sqlite_server`, `cmake_ctest_server`, `json_file_server`, `csv_server`, `compile_commands_server`, `jsonl_server`, `log_triage_server`, `sdk_smoke`, `async_request_matrix`, process/gateway probes |
| typed tool args/results and JSON schemas | `typed_tool_server`, `workspace_server`, `git_server`, `sqlite_server`, `cmake_ctest_server`, `json_file_server`, `csv_server`, `compile_commands_server`, `jsonl_server`, `log_triage_server`, `extension_plugin_adapter_matrix` |
| official MCP conformance server/client surface | `conformance_everything_server`, `conformance_everything_client` |
| typed SDK authoring API conformance smoke | `conformance_typed_authoring_server` |
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
| cancellation, progress, list-changed, elicitation-complete, task-status notifications | `timeout_cancellation_client`, `client_inbound_cancellation_matrix`, `client_callbacks_matrix`, `sdk_smoke`, `async_request_matrix` |
| raw/custom requests and notifications | `minimal_stdio_server`, `workspace_server`, `log_triage_server`, `sdk_smoke`, `client_callbacks_matrix` |
| RequestOptions, RequestHandle, async helpers, timeout/cancel, list_all helpers and cursor pagination | `timeout_cancellation_client`, `async_request_matrix`, `pagination_cursor_matrix`, `sdk_smoke` |
| role-generic transport contract | `transport_stdio_matrix`, `transport_adapter_matrix` |
| child-process stdio transport, `ClientPeer::connect_stdio`, and `mcp::serve` | `process_stdio_client_probe` |
| standalone streamable HTTP client | `streamable_http_client` |
| streamable HTTP client/server via gateway runtime | `http_gateway_runtime_matrix` |
| direct streamable HTTP server/client and legacy SSE client path | `direct_http_legacy_sse_matrix` |
| HTTP auth-lite: Authorization header, auth identity, 401 unauthorized mapping, WWW-Authenticate configuration | `http_auth_lite_matrix`, `policy_subscription_matrix` |
| auth provider and rate limiter hooks | `policy_subscription_matrix`, `http_auth_lite_matrix` |
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

## Validation Targets

- `cxxmcp_client_callbacks_matrix`: a client-side matrix for server-to-client
  features. It handles `roots/list`, `sampling/createMessage`,
  `elicitation/create`, custom requests, cancellation-aware request handlers,
  progress, logging, list changed notifications, and resource update
  notifications.
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
  `ToolRegistry`. This target is built only when the consumed SDK package still
  exports `cxxmcp::plugin_sdk` and `cxxmcp::adapters`.
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
- `cxxmcp_http_auth_lite_matrix`: runs a direct streamable HTTP server through
  `ServerPeer` + `Service` with an `AuthProvider`, a configured
  `WWW-Authenticate` challenge, and a `ClientPeer` streamable HTTP endpoint
  using the bearer-token helper; it verifies unauthorized `401` failures and
  authenticated `ToolContext::auth_identity` propagation.
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
  config generation, readiness/status checks, onboarding, config import, server
  discovery, and exposure profile management.

## MCP Conformance

The conformance harness is maintained separately at
`modelcontextprotocol/conformance`. Start the C++ everything server first:

```powershell
cmake --build build --target cxxmcp_conformance_everything_server --config Release
.\build\cxxmcp_conformance_everything_server.exe 3000
```

Then run the active server suite from a conformance checkout:

```powershell
npm start -- server --url http://127.0.0.1:3000/mcp --suite active --verbose
```

Current validation against the active suite is 40 passing checks and 0 failures.
No expected-failure baseline is currently required.

`cxxmcp_conformance_everything_server` is intentionally a protocol fixture: it
uses explicit JSON tool definitions for the broad official scenarios.
`cxxmcp_conformance_typed_authoring_server` is the smaller SDK-authoring
fixture: it registers tools through `mcp::server::tool<Args, Result>()` and
`ServerPeer::builder().tool<Args, Result>()` so `tools/list` catches typed API
schema regressions such as scalar arguments being advertised as a non-object
root schema.

To smoke the typed authoring fixture with the official conformance runner:

```powershell
cmake --build build --target cxxmcp_conformance_typed_authoring_server --config Release
.\build\cxxmcp_conformance_typed_authoring_server.exe 3001
npm start -- server --url http://127.0.0.1:3001/mcp --scenario tools-list
```

The same fixture can also be checked with the upstream MCP Inspector CLI:

```powershell
npx -y @modelcontextprotocol/inspector@0.22.0 --cli "http://127.0.0.1:3001/mcp" --transport http --method tools/list
```

For an opt-in CTest wrapper that starts the fixture, runs Inspector, and asserts
that `shout_scalar` and `shout_object` advertise object-root schemas, configure
with:

```powershell
cmake -S . -B build-inspector -DCXXMCP_ENABLE_INSPECTOR_SMOKE=ON
cmake --build build-inspector --target cxxmcp_conformance_typed_authoring_server --config Release
ctest --test-dir build-inspector -C Release -R cxxmcp_inspector_typed_authoring_smoke --output-on-failure
```

This Inspector smoke is intentionally opt-in for now because it depends on npm
and current Windows runs can produce a successful `tools/list` response before
the Inspector process exits with an upstream shutdown assertion.

To validate the latest dated MCP spec without draft-only 2026 scenarios:

```powershell
npm start -- server --url http://127.0.0.1:3000/mcp --suite all --spec-version 2025-11-25
```

Current server validation for `2025-11-25` is 47 passing checks and 0 failures.
Current server validation for the latest all-suite run is 108 passing checks
and 1 failure. The remaining failure is the SEP-2243
`ServerRejectsMissingMethodHeader` check: strict `Mcp-Method` rejection would
break the TypeScript SDK v1.29.0 conformance client, which does not yet send
that required header. The C++ client transport sends `Mcp-Method`; strict
server enforcement is tracked separately from the default compatibility mode.
Upstream tracking:
[typescript-sdk#2176](https://github.com/modelcontextprotocol/typescript-sdk/issues/2176)
and
[conformance#323](https://github.com/modelcontextprotocol/conformance/issues/323).

For client-mode conformance, build the client harness and run suites from the
conformance checkout:

```powershell
cmake --build build --target cxxmcp_conformance_everything_client --config Release
cd C:\Users\cmx\repo\conformance
npm start -- client --command "C:\Users\cmx\repo\cxxmcp-examples\build\cxxmcp_conformance_everything_client.exe" --suite core
npm start -- client --command "C:\Users\cmx\repo\cxxmcp-examples\build\cxxmcp_conformance_everything_client.exe" --suite draft
npm start -- client --command "C:\Users\cmx\repo\cxxmcp-examples\build\cxxmcp_conformance_everything_client.exe" --suite all
```

Current client validation without a client baseline:

- OpenSSL/vcpkg all suite: 428 passing checks and 8 failing checks.
- `2025-11-25` suite: 224 passing checks and 1 failing check.
- Tier auth suite: 217 passing checks and 0 failures.

The client auth harness passes the current tier, back-compat, draft, and
extension auth scenarios when built with `CXXMCP_AUTH_CRYPTO=OpenSSL`,
including private_key_jwt client credentials and enterprise-managed
authorization. The remaining client all-suite failures are SSE retry /
`Last-Event-ID` behavior and SEP-2243 custom header validation cases.

In local `--suite all` comparisons against the same runner, the C++ server is
materially ahead of the saved RMCP reference run: 108/1 for the C++ server all
suite versus 48/47 for RMCP. The C++ client all suite produces a complete
428/8 summary; the RMCP client all run did not produce an all-suite summary
because the runner crashed after RMCP returned an empty/non-JSON response. See
[`CONFORMANCE_STATUS.md`](../CONFORMANCE_STATUS.md) for exact commands, known
exceptions, and raw stderr notes.
