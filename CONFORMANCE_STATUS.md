# MCP Conformance Status

Date: 2026-05-29 (updated — SSE retry fixed)

This document records the current server/client conformance status for the C++
SDK examples. It intentionally separates real SDK coverage from raw protocol
probes so failures are not hidden by baselines or harness shortcuts.

## Local Build

Current adjacent SDK source: `../MCPServer.cpp`.

Commands run:

```powershell
cmake --build build --config Debug
```

Result:

- Build passed.
- Adjacent SDK verification in `../MCPServer.cpp` also passed for
  `mcp_client` and `mcp_server` with both `CXXMCP_ENABLE_AUTH=OFF` and
  `CXXMCP_ENABLE_AUTH=ON`.
- CTest was not re-run after the adjacent SDK update.

## Server Conformance

Server harness:

```powershell
.\build\cxxmcp_conformance_everything_server.exe 3000
```

### Server Active Suite

Command:

```powershell
npm start -- server --url http://127.0.0.1:3000/mcp --suite active
```

Current result:

- 40 passed.
- 0 failed.

No expected-failure baseline is currently required.

### Server 2025-11-25 Suite

Command:

```powershell
npm start -- server --url http://127.0.0.1:3000/mcp --suite all --spec-version 2025-11-25
```

Current summary:

- 47 passed.
- 0 failed.

### Server All Suite

Command:

```powershell
npm start -- server --url http://127.0.0.1:3000/mcp --suite all
```

Current summary:

- 109 passed.
- 1 failed.

Failing scenarios:

| Scenario | Result | Root Cause |
| --- | --- | --- |
| `http-header-validation` | 11 passed, 1 failed | Conformance suite contradiction: test expects `Mcp-Method` required, but TypeScript SDK client doesn't send it |

The single remaining failure (`ServerRejectsMissingMethodHeader`) is a **known
upstream issue** — not a server bug. It cannot be resolved on the server side
without breaking 32 other SDK-backed scenarios (the TypeScript SDK does not send
`Mcp-Method`, so requiring it rejects all SDK client requests). Issues filed:

- [conformance#323](https://github.com/modelcontextprotocol/conformance/issues/323)
- [typescript-sdk#2176](https://github.com/modelcontextprotocol/typescript-sdk/issues/2176)

**Resolution**: 109/1 is the expected final state until the TypeScript SDK
implements SEP-2243 header support. No server-side workaround is appropriate.

The previous input-required-result / MRTR failures now pass in the local
all-suite run.

## Client Conformance

Client harness:

```powershell
.\build\cxxmcp_conformance_everything_client.exe <scenario-server-url>
```

The conformance runner sets `MCP_CONFORMANCE_SCENARIO` and appends the scenario
server URL.

### SDK-Backed Client Coverage

The default client harness counts only SDK-backed paths. Raw protocol probes are
not counted as SDK coverage.

Default auth/no-OpenSSL build command:

```powershell
npm start -- client --command "C:\Users\cmx\repo\cxxmcp-examples\build\cxxmcp_conformance_everything_client.exe" --suite all --timeout 30000
```

Current summary:

- 442 passed.
- 1 failed.
- 0 warnings.

This build has `CXXMCP_AUTH_CRYPTO=NONE`, so
`auth/client-credentials-jwt` is expected to remain failed because
private_key_jwt signing is compiled only with the optional OpenSSL auth backend.

OpenSSL/vcpkg build command:

```powershell
cmake -S . -B build-auth-openssl -G Ninja -DCMAKE_SKIP_INSTALL_RULES=ON -DCXXMCP_ENABLE_AUTH=ON -DCXXMCP_ENABLE_HTTP=ON -DCXXMCP_AUTH_CRYPTO=OpenSSL -DCMAKE_TOOLCHAIN_FILE=C:\Users\cmx\repo\vcpkg\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build-auth-openssl --target cxxmcp_conformance_everything_client
npm start -- client --command "C:\Users\cmx\repo\cxxmcp-examples\build-auth-openssl\cxxmcp_conformance_everything_client.exe" --suite all --timeout 30000
```

OpenSSL current summary:

- 448 passed.
- 0 failed.
- 0 warnings.

Auth status in the OpenSSL build:

- Tier auth suite: 217 passed, 0 failed.
- Back-compat auth: both 2025-03-26 scenarios passed.
- Draft auth: resource mismatch, offline access, AS migration, ISS parameter,
  and metadata issuer mismatch scenarios passed.
- Extension auth: `client-credentials-jwt`, `client-credentials-basic`, and
  `enterprise-managed-authorization` passed.

### Client 2025-11-25 Suite

Command:

```powershell
npm start -- client --command "C:\Users\cmx\repo\cxxmcp-examples\build-auth-openssl\cxxmcp_conformance_everything_client.exe" --suite all --spec-version 2025-11-25 --timeout 30000
```

Current summary:

- 224 passed.
- 0 failed.
- 0 warnings.

OpenSSL current all passing scenarios/checks:

| Scenario | Result |
| --- | --- |
| `initialize` | 1 passed, 0 failed |
| `tools_call` | 1 passed, 0 failed |
| `elicitation-sep1034-client-defaults` | 5 passed, 0 failed |
| `request-metadata` | 7 passed, 0 failed |
| `auth/*` tier/backcompat/draft/extension scenarios | all passed |
| `sep-2322-client-request-state` | 5 passed, 0 failed |
| `http-standard-headers` | 11 passed, 0 failed |
| `http-custom-headers` | 18 passed, 0 failed |
| `http-invalid-tool-headers` | 11 passed, 0 failed |
| `json-schema-ref-no-deref` | 1 passed, 0 failed |
| `sse-retry` | 3 passed, 0 failed |

No failing client scenarios in the OpenSSL build.
```

## Raw Protocol Probes

No raw HTTP/JSON-RPC probes are currently counted as SDK client conformance.
Previous raw probes for `request-metadata`, `sep-2322-client-request-state`,
and `http-standard-headers` were removed from the default path. Current passes
for `request-metadata`, `sep-2322-client-request-state`, and
`http-standard-headers` use SDK transport/request APIs.

## RMCP Reference Comparison

RMCP saved audit files were found under
`../MCPServer.cpp/reference/rmcp/conformance/results/`. The requested
`../conformance/results/` directory does not exist locally.

The saved RMCP reports are dated 2026-02-25 and use an older scoring shape:

- Server: 83.3% scenario pass rate, 25/30 scenarios, 5 failing scenarios.
- Client: 85.0% date-versioned scenario pass rate, 17/20 scenarios, 3 failing
  scenarios.
- Final tier: Tier 3, blocked by triage, labels, stable release, documentation,
  and roadmap/versioning gaps.

Current local RMCP binaries were also run against the same local conformance
runner on 2026-05-29. The fair headline comparison uses `--suite all` on both
server and client:

- Server all suite: 48 passed, 47 failed.
  - Major failures include `server-stateless`, `caching`,
    `http-header-validation`, `http-custom-header-server-validation`, and the
    `input-required-result-*` draft scenarios.
- Client all suite did not produce a summary. The conformance runner crashed in
  `request-metadata.ts` with `SyntaxError: Unexpected end of JSON input` after
  RMCP returned an empty/non-JSON response.

Other RMCP sub-suite results are not used for the headline comparison because
they are not all-suite equivalent. For debugging only, RMCP server active was
40 passed / 2 failed and RMCP client auth was 190 passed / 17 failed / 2
warnings.

All-suite comparison:

- Server all: C++ currently reports 109 passed and 1 failed versus RMCP 48
  passed and 47 failed.
- Client all: C++ produces a complete SDK-only summary. With the optional
  OpenSSL auth backend enabled it reports 448 passed and 0 failed. RMCP
  currently crashes the runner before an all-suite summary.

## Main SDK Gaps

1. **Server `http-header-validation` (1 failure, upstream)**:
   `ServerRejectsMissingMethodHeader` expects HTTP 400 when `Mcp-Method` header
   is missing. Our C++ SDK client sends `Mcp-Method` correctly, but the
   conformance runner's TypeScript SDK client (`@modelcontextprotocol/sdk`
   v1.29.0) does not. Making `Mcp-Method` required breaks 32 SDK-backed
   scenarios. This is a conformance suite contradiction, not a server bug.
   Cannot be resolved server-side. Filed as
   [conformance#323](https://github.com/modelcontextprotocol/conformance/issues/323)
   and [typescript-sdk#2176](https://github.com/modelcontextprotocol/typescript-sdk/issues/2176).
   **Expected to resolve upstream when TypeScript SDK implements SEP-2243.**
2. Client private_key_jwt requires the optional OpenSSL auth backend; the
   no-OpenSSL build deliberately reports that scenario unsupported.
