# MCP Conformance Status

Date: 2026-09-15

This document records the current server/client conformance status for the C++
SDK examples. It intentionally separates real SDK coverage from raw protocol
probes so failures are not hidden by baselines or harness shortcuts.

## Local Build

Current adjacent SDK source: `../cxxmcp` (master, including the draft
conformance work merged in `a0e3134`).

Commands run:

```powershell
cmake --build build
```

Result:

- Build passed with `CXXMCP_ENABLE_AUTH=ON`, `CXXMCP_ENABLE_HTTP=ON`, and
  `CXXMCP_AUTH_CRYPTO=OpenSSL` (vcpkg toolchain).

## Server Conformance

Server harness:

```powershell
.\build\cxxmcp_conformance_everything_server.exe 3000
```

### Server Active Suite

Command:

```powershell
node dist/index.js server --url http://127.0.0.1:3100/mcp --suite active
```

Current result:

- 72 passed.
- 0 failed.

No expected-failure baseline is required.

### Server 2025-11-25 Suite

Command:

```powershell
node dist/index.js server --url http://127.0.0.1:3101/mcp --suite all --spec-version 2025-11-25
```

Current summary:

- 80 passed.
- 0 failed.

### Server All Suite

Command:

```powershell
node dist/index.js server --url http://127.0.0.1:3100/mcp --suite all
```

Current summary:

- 272 passed.
- 0 failed.

All scenarios pass, including the previously failing
`http-header-validation / ServerRejectsMissingMethodHeader` check. The SDK
enforces strict SEP-2243 standard-header validation on the stateless
(SEP-2575) wire while still tolerating stateful requests from the TypeScript
SDK client that omit `Mcp-Method`, so the historical upstream contradiction
(conformance#323, typescript-sdk#2176) no longer produces a failure.

Newly covered draft surface includes SEP-2575 stateless lifecycle,
`subscriptions/listen` streaming with publish fan-out, SEP-2663 task
methods with extension gating, SEP-2640 skills fixtures, `server/discover`
`_meta.serverInfo` + `ttlMs`/`cacheScope` fields, and `-32020`/`-32021`/
`-32022` error-code to HTTP-status mapping.

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

OpenSSL/vcpkg build command:

```powershell
cmake -S . -B build -G Ninja -DCXXMCP_ENABLE_AUTH=ON -DCXXMCP_ENABLE_HTTP=ON -DCXXMCP_AUTH_CRYPTO=OpenSSL -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --target cxxmcp_conformance_everything_client
node dist/index.js client --command "D:\repo\cxxmcp-examples\build\cxxmcp_conformance_everything_client.exe" --suite all --timeout 30000
```

OpenSSL current summary:

- 501 passed.
- 0 failed.
- 0 warnings.

Auth status in the OpenSSL build:

- Tier auth suite: 227 passed, 0 failed, 0 warnings.
- Back-compat auth: both 2025-03-26 scenarios passed.
- Draft auth: resource mismatch, offline access, AS migration (including
  re-registration at the migrated authorization server), ISS parameter, and
  metadata issuer mismatch scenarios passed.
- Extension auth: `client-credentials-jwt`, `client-credentials-basic`, and
  `enterprise-managed-authorization` passed.
- DPoP (RFC 9449): `auth/dpop` 12/12 and `auth/dpop-nonce` 14/14 —
  proof-per-request, `Authorization: DPoP`, and AS/RS `DPoP-Nonce` retry.
- `auth/wif-jwt-bearer`: 8/8.
- The no-OpenSSL build does not support `private_key_jwt`, DPoP signing, or
  the WIF grant — those scenarios are expected to report unsupported there.

### Client 2025-11-25 Suite

Command:

```powershell
node dist/index.js client --command "D:\repo\cxxmcp-examples\build\cxxmcp_conformance_everything_client.exe" --suite all --spec-version 2025-11-25 --timeout 30000
```

Current summary:

- 247 passed.
- 0 failed.
- 0 warnings.

No failing client scenarios in the OpenSSL build.

## Raw Protocol Probes

No raw HTTP/JSON-RPC probes are counted as SDK client conformance.

## RMCP Reference Comparison

RMCP saved audit files were found under
`../cxxmcp/reference/rmcp/conformance/results/`. The requested
`../conformance/results/` directory does not exist locally.

The saved RMCP reports are dated 2026-02-25 and use an older scoring shape:

- Server: 83.3% scenario pass rate, 25/30 scenarios, 5 failing scenarios.
- Client: 85.0% date-versioned scenario pass rate, 17/20 scenarios, 3 failing
  scenarios.
- Final tier: Tier 3, blocked by triage, labels, stable release, documentation,
  and roadmap/versioning gaps.

Current local RMCP binaries were last run against the same local conformance
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

- Server all: C++ currently reports 272 passed and 0 failed versus RMCP 48
  passed and 47 failed.
- Client all: C++ produces a complete SDK-only summary. With the optional
  OpenSSL auth backend enabled it reports 501 passed and 0 failed. RMCP
  currently crashes the runner before an all-suite summary.

## Main SDK Gaps

- Client `private_key_jwt`, DPoP, and WIF signing require the optional
  OpenSSL auth backend; the no-OpenSSL build deliberately reports those
  scenarios unsupported.
- The previously tracked SEP-2243 `ServerRejectsMissingMethodHeader`
  exception is resolved (see Server All Suite above).
