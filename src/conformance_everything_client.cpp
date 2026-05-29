#include <chrono>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cxxmcp/peer.hpp"
#include "cxxmcp/protocol/capabilities.hpp"
#include "cxxmcp/protocol/elicitation.hpp"
#include "cxxmcp/service.hpp"

#if defined(CXXMCP_EXAMPLES_ENABLE_AUTH)
#include "cxxmcp/auth/http_metadata_endpoint.hpp"
#include "cxxmcp/auth/http_token_endpoint.hpp"
#include "cxxmcp/auth/lifecycle.hpp"
#include "cxxmcp/auth/registration.hpp"
#if defined(CXXMCP_EXAMPLES_ENABLE_AUTH_OPENSSL)
#include "cxxmcp/auth/openssl/dpop.hpp"
#endif
#include "cxxmcp/client/http_transport.hpp"
#include "httplib/httplib.h"
#endif

namespace {

using Json = mcp::protocol::Json;

[[noreturn]] void fail(std::string message) {
  throw std::runtime_error(std::move(message));
}

void require(bool condition, std::string_view message) {
  if (!condition) {
    fail(std::string(message));
  }
}

template <class T>
T unwrap(mcp::core::Result<T> result, std::string_view label) {
  if (!result.has_value()) {
    fail(std::string(label) + ": " + result.error().message);
  }
  return std::move(*result);
}

mcp::protocol::ClientCapabilities standard_capabilities() {
  return mcp::protocol::client_capabilities()
      .roots()
      .sampling()
      .elicitation_form()
      .build();
}

struct RunningClient {
  mcp::RunningService<mcp::RoleClient> service;

  explicit RunningClient(mcp::RunningService<mcp::RoleClient> running)
      : service(std::move(running)) {}
  RunningClient(const RunningClient&) = delete;
  RunningClient& operator=(const RunningClient&) = delete;
  RunningClient(RunningClient&&) noexcept = default;
  RunningClient& operator=(RunningClient&&) noexcept = default;

  ~RunningClient() { (void)service.stop(); }

  mcp::ClientPeer& peer() noexcept { return service.peer(); }
};

RunningClient connect_client(std::string server_url,
                             bool elicitation_defaults = false) {
  auto builder = mcp::ClientPeer::builder();
  builder.streamable_http(std::move(server_url))
      .capabilities(standard_capabilities())
      .roots({mcp::protocol::Root{.uri = "file:///workspace",
                                  .name = "workspace"}});

  if (elicitation_defaults) {
    builder.on_create_elicitation_request(
        [](const mcp::protocol::CreateElicitationRequestParam&)
            -> mcp::core::Result<mcp::protocol::CreateElicitationResult> {
          return mcp::protocol::CreateElicitationResult{
              .action = mcp::protocol::ElicitationAction::Accept,
              .content = Json{{"name", "John Doe"},
                              {"age", 30},
                              {"score", 95.5},
                              {"status", "active"},
                              {"verified", true}},
          };
        });
  }

  auto built = unwrap(builder.build(), "client build failed");
  auto running =
      unwrap(mcp::serve(std::move(built)), "client service failed to start");
  RunningClient client(std::move(running));

  unwrap(client.peer().initialize("cxxmcp-conformance-client", "0.1.0"),
         "initialize failed");
  unwrap(client.peer().notify_initialized(), "initialized notification failed");
  return std::move(client);
}

void run_initialize_like(const std::string& server_url) {
  auto client = connect_client(server_url);
  unwrap(client.peer().list_tools(), "tools/list failed");
}

void run_tools_call(const std::string& server_url) {
  auto client = connect_client(server_url);
  const auto tools = unwrap(client.peer().list_tools(), "tools/list failed");
  bool found = false;
  for (const auto& tool : tools) {
    if (tool.name == "add_numbers") {
      found = true;
      break;
    }
  }
  require(found, "add_numbers tool was not advertised");
  unwrap(client.peer().call_tool("add_numbers", Json{{"a", 2}, {"b", 3}}),
         "tools/call add_numbers failed");
}

void run_elicitation_defaults(const std::string& server_url) {
  auto client = connect_client(server_url, true);
  const auto tools = unwrap(client.peer().list_tools(), "tools/list failed");
  bool found = false;
  for (const auto& tool : tools) {
    if (tool.name == "test_client_elicitation_defaults") {
      found = true;
      break;
    }
  }
  require(found, "test_client_elicitation_defaults tool was not advertised");
  unwrap(client.peer().call_tool("test_client_elicitation_defaults",
                                 Json::object()),
         "elicitation defaults tool call failed");
}

void run_http_standard_headers(const std::string& server_url) {
  // Build peer directly (no RunningService) to avoid SSE stream lifecycle.
  auto builder = mcp::ClientPeer::builder();
  builder.streamable_http(std::string(server_url))
      .capabilities(standard_capabilities())
      .roots({mcp::protocol::Root{.uri = "file:///workspace",
                                  .name = "workspace"}})
      .timeout(std::chrono::milliseconds{5000});

  auto peer = unwrap(builder.build(), "client build failed");

  unwrap(peer.initialize("cxxmcp-conformance-client", "0.1.0"),
         "initialize failed");
  unwrap(peer.notify_initialized(), "initialized notification failed");

  // The conformance test checks HTTP headers on each request.
  // SDK call results may be errors, but headers are validated by the harness.
  (void)peer.list_tools();
  (void)peer.call_tool("test_headers", Json::object());
  (void)peer.list_resources();
  (void)peer.read_resource("file:///path/to/file%20name.txt");
  (void)peer.list_prompts();
  (void)peer.get_prompt("test_prompt", Json::object());
}

Json request_metadata_params(const std::string& version) {
  return Json{{"_meta",
               Json{{"io.modelcontextprotocol/protocolVersion", version},
                    {"io.modelcontextprotocol/clientInfo",
                     Json{{"name", "cxxmcp-conformance-client"},
                          {"version", "0.1.0"}}},
                    {"io.modelcontextprotocol/clientCapabilities",
                     Json{{"roots", Json::object()},
                          {"sampling", Json::object()},
                          {"elicitation", Json::object()}}}}}};
}

void run_request_metadata(const std::string& server_url) {
  constexpr std::string_view kDraftVersion = "DRAFT-2026-v1";

  auto meta = request_metadata_params(std::string(kDraftVersion))["_meta"];

  mcp::RequestOptions opts;
  opts.meta = meta;
  opts.protocol_version = std::string(kDraftVersion);

  auto builder = mcp::ClientPeer::builder();
  builder.streamable_http(std::string(server_url))
      .capabilities(standard_capabilities())
      .roots({mcp::protocol::Root{.uri = "file:///workspace",
                                  .name = "workspace"}});

  auto built = unwrap(builder.build(), "client build failed");
  auto running =
      unwrap(mcp::serve(std::move(built)), "client service failed to start");
  RunningClient client(std::move(running));

  unwrap(client.peer().initialize("cxxmcp-conformance-client", "0.1.0",
                                  std::move(opts)),
         "initialize with metadata failed");

  mcp::RequestOptions notify_opts;
  notify_opts.meta = std::move(meta);
  unwrap(client.peer().notify_initialized(std::move(notify_opts)),
         "initialized notification failed");
}

Json make_mrtr_input_responses(const mcp::protocol::ToolResult& result) {
  if (!result.input_requests.has_value()) {
    return Json::object();
  }
  Json responses = Json::object();
  const auto& requests = *result.input_requests;
  for (auto it = requests.begin(); it != requests.end(); ++it) {
    const auto& request = it.value();
    if (request.contains("method") &&
        request.at("method") == "elicitation/create") {
      responses[it.key()] =
          Json{{"action", "accept"}, {"content", Json{{"confirmed", true}}}};
    }
  }
  return responses;
}

void run_mrtr_client(const std::string& server_url) {
  // MRTR conformance server doesn't implement initialize — connect directly.
  auto builder = mcp::ClientPeer::builder();
  builder.streamable_http(std::string(server_url))
      .capabilities(standard_capabilities())
      .roots({mcp::protocol::Root{.uri = "file:///workspace",
                                  .name = "workspace"}});

  auto built = unwrap(builder.build(), "client build failed");
  auto running =
      unwrap(mcp::serve(std::move(built)), "client service failed to start");
  RunningClient client(std::move(running));

  // Round 1: echo_state — get requestState and inputRequests
  mcp::protocol::ToolCall echo_call;
  echo_call.name = "test_mrtr_echo_state";
  echo_call.arguments = Json::object();
  auto echo_result =
      unwrap(client.peer().call_tool(echo_call), "test_mrtr_echo_state failed");
  require(echo_result.input_requests.has_value(),
          "test_mrtr_echo_state: missing inputRequests");
  require(echo_result.request_state.has_value(),
          "test_mrtr_echo_state: missing requestState");

  auto echo_responses = make_mrtr_input_responses(echo_result);

  // Unrelated tool call between rounds
  unwrap(client.peer().call_tool("test_mrtr_unrelated", Json::object()),
         "test_mrtr_unrelated failed");

  // Round 2: echo_state with inputResponses + requestState
  mcp::protocol::ToolCall echo_call2;
  echo_call2.name = "test_mrtr_echo_state";
  echo_call2.arguments = Json::object();
  echo_call2.input_responses = echo_responses;
  echo_call2.request_state = echo_result.request_state;
  unwrap(client.peer().call_tool(echo_call2),
         "test_mrtr_echo_state round 2 failed");

  // Round 1: no_state — get inputRequests (no requestState expected)
  mcp::protocol::ToolCall no_state_call;
  no_state_call.name = "test_mrtr_no_state";
  no_state_call.arguments = Json::object();
  auto no_state_result = unwrap(client.peer().call_tool(no_state_call),
                                "test_mrtr_no_state failed");
  require(no_state_result.input_requests.has_value(),
          "test_mrtr_no_state: missing inputRequests");

  auto no_state_responses = make_mrtr_input_responses(no_state_result);

  // Round 2: no_state with inputResponses only (no requestState)
  mcp::protocol::ToolCall no_state_call2;
  no_state_call2.name = "test_mrtr_no_state";
  no_state_call2.arguments = Json::object();
  no_state_call2.input_responses = no_state_responses;
  unwrap(client.peer().call_tool(no_state_call2),
         "test_mrtr_no_state round 2 failed");

  // Single-shot: no_result_type
  mcp::protocol::ToolCall no_result_call;
  no_result_call.name = "test_mrtr_no_result_type";
  no_result_call.arguments = Json::object();
  unwrap(client.peer().call_tool(no_result_call),
         "test_mrtr_no_result_type failed");
}

void run_http_custom_headers(const std::string& server_url) {
  auto client = connect_client(server_url);
  const auto tools = unwrap(client.peer().list_tools(), "tools/list failed");

  // Call test_custom_headers with the expected arguments.
  // The SDK automatically generates Mcp-Param-* headers from the cached schema.
  Json arguments = {{"region", "us-west1"},
                    {"priority", 42},
                    {"verbose", false},
                    {"debug", true},
                    {"empty_val", ""},
                    {"method_val", "test-method"},
                    {"float_val", 3.14159},
                    {"non_ascii_val", "Hello, \xe4\xb8\x96\xe7\x95\x8c"},
                    {"whitespace_val", " padded "},
                    {"leading_space_val", " us-west1"},
                    {"trailing_space_val", "us-west1 "},
                    {"internal_space_val", "us west 1"},
                    {"control_char_val", "line1\nline2"},
                    {"crlf_val", "line1\r\nline2"},
                    {"tab_val", "\tindented"},
                    {"query", "SELECT * FROM users"}};
  unwrap(client.peer().call_tool("test_custom_headers", arguments),
         "test_custom_headers failed");

  // Call test_custom_headers_null (verbose=null → header omitted).
  Json null_arguments = {{"region", "us-east1"},
                         {"priority", 1},
                         {"verbose", nullptr},
                         {"query", "SELECT 1"}};
  unwrap(client.peer().call_tool("test_custom_headers_null", null_arguments),
         "test_custom_headers_null failed");
}

void run_http_invalid_tool_headers(const std::string& server_url) {
  auto client = connect_client(server_url);
  const auto tools = unwrap(client.peer().list_tools(), "tools/list failed");

  // Find and call valid_tool — the SDK filters invalid tools via
  // validate_tool_x_headers, but the harness must also avoid calling them.
  bool found_valid = false;
  for (const auto& tool : tools) {
    auto entries = mcp::protocol::extract_x_mcp_headers(tool.input_schema);
    if (tool.name == "valid_tool" &&
        mcp::protocol::validate_tool_x_headers(entries)) {
      found_valid = true;
      unwrap(client.peer().call_tool("valid_tool", Json{{"region", "us-east1"}}),
             "valid_tool failed");
      break;
    }
  }
  require(found_valid, "valid_tool not found or was incorrectly rejected");
}

void run_sse_retry(const std::string& server_url) {
  auto client = connect_client(server_url);
  const auto tools = unwrap(client.peer().list_tools(), "tools/list failed");
  // Call test_reconnection — the server responds with an SSE stream containing
  // a priming event (retry hint) and closes the stream. The SDK client must
  // wait the retry duration and reconnect via GET with Last-Event-ID.
  unwrap(client.peer().call_tool("test_reconnection", Json::object()),
         "test_reconnection failed");
}

#if defined(CXXMCP_EXAMPLES_ENABLE_AUTH)

struct UrlParts {
  std::string origin;
  std::string path;
};

UrlParts parse_http_url(const std::string& url) {
  httplib::detail::UrlComponents components;
  if (!httplib::detail::parse_url(url, components) ||
      components.host.empty()) {
    fail("invalid URL: " + url);
  }
  const bool is_ssl = components.scheme == "https";
  int port = is_ssl ? 443 : 80;
  if (!components.port.empty() &&
      !httplib::detail::parse_port(components.port, port)) {
    fail("invalid URL port: " + url);
  }
  std::string origin = is_ssl ? "https://" : "http://";
  origin +=
      httplib::detail::make_host_and_port_string(components.host, port, is_ssl);

  std::string path = components.path.empty() ? "/" : components.path;
  if (!components.query.empty()) {
    path.push_back('?');
    path += components.query;
  }
  return UrlParts{std::move(origin), std::move(path)};
}

std::string url_origin(const std::string& url) {
  return parse_http_url(url).origin;
}

httplib::Headers to_httplib_headers(const mcp::auth::HeaderMap& headers) {
  httplib::Headers result;
  for (const auto& [name, value] : headers) {
    result.emplace(name, value);
  }
  return result;
}

mcp::auth::HeaderMap from_httplib_headers(const httplib::Headers& headers) {
  mcp::auth::HeaderMap result;
  for (const auto& [name, value] : headers) {
    result[name] = value;
  }
  return result;
}

mcp::core::Result<mcp::auth::OAuthHttpResponse> oauth_http_get(
    const mcp::auth::MetadataFetchRequest& request) {
  const auto parts = parse_http_url(request.url);
  httplib::Client client(parts.origin);
  auto response = client.Get(parts.path, to_httplib_headers(request.headers));
  if (!response) {
    return mcp::core::unexpected(mcp::auth::make_oauth_error(
        mcp::auth::OAuthErrorCode::kMetadataDiscoveryFailed,
        "metadata HTTP GET failed", httplib::to_string(response.error())));
  }
  return mcp::auth::OAuthHttpResponse{
      response->status, from_httplib_headers(response->headers),
      response->body};
}

mcp::core::Result<mcp::auth::OAuthHttpResponse> oauth_http_post(
    const mcp::auth::OAuthHttpRequest& request) {
  const auto parts = parse_http_url(request.url);
  httplib::Client client(parts.origin);
  const auto content_type =
      request.headers.count("Content-Type") != 0
          ? request.headers.at("Content-Type")
          : std::string("application/x-www-form-urlencoded");
  auto response = client.Post(parts.path, to_httplib_headers(request.headers),
                              request.body, content_type);
  if (!response) {
    return mcp::core::unexpected(mcp::auth::make_oauth_error(
        mcp::auth::OAuthErrorCode::kTokenExchangeFailed,
        "OAuth HTTP POST failed", httplib::to_string(response.error())));
  }
  return mcp::auth::OAuthHttpResponse{
      response->status, from_httplib_headers(response->headers),
      response->body};
}

std::string percent_decode(std::string_view value) {
  std::string decoded;
  decoded.reserve(value.size());
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '+') {
      decoded.push_back(' ');
      continue;
    }
    if (value[i] == '%' && i + 2 < value.size() &&
        std::isxdigit(static_cast<unsigned char>(value[i + 1])) &&
        std::isxdigit(static_cast<unsigned char>(value[i + 2]))) {
      const auto hex = std::string(value.substr(i + 1, 2));
      decoded.push_back(static_cast<char>(std::stoi(hex, nullptr, 16)));
      i += 2;
      continue;
    }
    decoded.push_back(value[i]);
  }
  return decoded;
}

std::map<std::string, std::string> query_params(const std::string& url) {
  std::map<std::string, std::string> result;
  const auto query_start = url.find('?');
  if (query_start == std::string::npos) {
    return result;
  }
  auto query = std::string_view(url).substr(query_start + 1);
  const auto fragment = query.find('#');
  if (fragment != std::string_view::npos) {
    query = query.substr(0, fragment);
  }
  std::size_t pos = 0;
  while (pos <= query.size()) {
    const auto next = query.find('&', pos);
    const auto end = next == std::string_view::npos ? query.size() : next;
    const auto entry = query.substr(pos, end - pos);
    const auto equals = entry.find('=');
    if (equals != std::string_view::npos) {
      result[percent_decode(entry.substr(0, equals))] =
          percent_decode(entry.substr(equals + 1));
    }
    if (next == std::string_view::npos) {
      break;
    }
    pos = next + 1;
  }
  return result;
}

mcp::auth::PkceChallenge fixed_pkce() {
  return mcp::auth::PkceChallenge{
      "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk",
      "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM",
      mcp::auth::PkceCodeChallengeMethod::kS256};
}

class HttpClientRegistrationEndpoint final
    : public mcp::auth::OAuthClientRegistrationEndpoint {
 public:
  mcp::core::Result<mcp::auth::ClientRegistrationResponse> register_client(
      const mcp::auth::ClientRegistrationEndpointRequest& request) override {
    Json body = Json::object();
    body["redirect_uris"] = request.registration.redirect_uris;
    body["grant_types"] = request.registration.grant_types;
    body["response_types"] = request.registration.response_types;
    if (!request.registration.scope.empty()) {
      body["scope"] = join(request.registration.scope);
    }
    if (request.registration.client_name.has_value()) {
      body["client_name"] = *request.registration.client_name;
    }
    if (request.registration.token_endpoint_auth_method.has_value()) {
      body["token_endpoint_auth_method"] =
          *request.registration.token_endpoint_auth_method;
    }
    for (const auto& [name, value] : request.registration.metadata) {
      body[name] = value;
    }

    mcp::auth::OAuthHttpRequest http_request;
    http_request.url = request.registration_endpoint;
    http_request.headers = request.headers;
    http_request.headers["Accept"] = "application/json";
    http_request.headers["Content-Type"] = "application/json";
    http_request.body = body.dump();

    auto response = oauth_http_post(http_request);
    if (!response.has_value()) {
      return mcp::core::unexpected(response.error());
    }
    if (response->status_code < 200 || response->status_code >= 300) {
      return mcp::core::unexpected(mcp::auth::make_oauth_error(
          mcp::auth::OAuthErrorCode::kClientRegistrationFailed,
          "dynamic client registration returned non-success status",
          std::to_string(response->status_code)));
    }

    Json parsed = Json::parse(response->body);
    mcp::auth::ClientRegistrationResponse result;
    result.client_id = parsed.value("client_id", "");
    if (parsed.contains("client_secret") && parsed["client_secret"].is_string()) {
      result.client_secret = parsed["client_secret"].get<std::string>();
    }
    if (parsed.contains("token_endpoint_auth_method") &&
        parsed["token_endpoint_auth_method"].is_string()) {
      const auto method =
          parsed["token_endpoint_auth_method"].get<std::string>();
      result.client_metadata.token_endpoint_auth_method = method;
      result.metadata["token_endpoint_auth_method"] = method;
    }
    return result;
  }

 private:
  static std::string join(const std::vector<std::string>& values) {
    std::string result;
    for (const auto& value : values) {
      if (!result.empty()) {
        result.push_back(' ');
      }
      result += value;
    }
    return result;
  }
};

Json conformance_context() {
  const char* context_env = std::getenv("MCP_CONFORMANCE_CONTEXT");
  if (context_env == nullptr || *context_env == '\0') {
    return Json::object();
  }
  return Json::parse(context_env);
}

class ConformanceAuthSession {
 public:
  ConformanceAuthSession(std::string server_url, std::string scenario,
                         Json context)
      : server_url_(std::move(server_url)),
        scenario_(std::move(scenario)),
        context_(std::move(context)),
        metadata_endpoint_(oauth_http_get),
        token_endpoint_(oauth_http_post) {}

  std::optional<std::string> refresh(
      const mcp::client::HttpAuthChallenge& challenge) {
    auto decision = analyze(challenge);
    if (decision.action == mcp::auth::AuthResponseAction::kProceed) {
      return std::nullopt;
    }

    if (!configured_) {
      configure(decision);
    } else if (should_rediscover(decision)) {
      configure(decision, true);
    }

    if (scenario_ == "auth/client-credentials-basic") {
      auto token = authenticate_client_credentials();
      if (!token.has_value()) {
        fail("client credentials authorization failed: " +
             token.error().message);
      }
      return token->access_token;
    }
    if (scenario_ == "auth/client-credentials-jwt") {
      auto token = authenticate_client_credentials_jwt();
      if (!token.has_value()) {
        fail("client credentials JWT authorization failed: " +
             token.error().message);
      }
      return token->access_token;
    }
    if (scenario_ == "auth/enterprise-managed-authorization") {
      auto token = authenticate_enterprise_managed();
      if (!token.has_value()) {
        fail("enterprise managed authorization failed: " +
             token.error().message);
      }
      return token->access_token;
    }

    mcp::core::Result<mcp::auth::TokenSet> token;
    if (decision.action == mcp::auth::AuthResponseAction::kScopeUpgradeRequired &&
        decision.challenge.has_value()) {
      auto upgrade = unwrap(
          manager_.request_scope_upgrade(*decision.challenge, fixed_pkce(),
                                         next_state()),
          "scope upgrade authorization failed");
      token = complete_authorization(upgrade.url, upgrade.state.state);
    } else {
      token = authorize(decision);
    }
    if (!token.has_value()) {
      fail("authorization failed: " + token.error().message);
    }
    return token->access_token;
  }

 private:
  static constexpr std::string_view kCimdClientMetadataUrl =
      "https://conformance-test.local/client-metadata.json";

  mcp::auth::AuthResponseDecision analyze(
      const mcp::client::HttpAuthChallenge& challenge) {
    mcp::auth::HttpResponseMetadata response;
    response.status_code = challenge.status_code;
    for (const auto& [name, value] : challenge.headers) {
      response.headers[name] = value;
    }
    if (challenge.www_authenticate.has_value()) {
      response.headers["WWW-Authenticate"] = *challenge.www_authenticate;
    }
    return unwrap(mcp::auth::analyze_auth_response(response),
                  "auth challenge analysis failed");
  }

  void configure(const mcp::auth::AuthResponseDecision& decision,
                 bool reset = false) {
    if (reset) {
      manager_ = mcp::auth::AuthorizationManager{};
      configured_ = false;
      preconfigured_client_ = false;
    }

    mcp::auth::MetadataDiscoveryExecutor discovery(metadata_endpoint_);
    auto discovered_result = discovery.discover(server_url_, decision);
    if (!discovered_result.has_value() && is_legacy_2025_03_26_scenario()) {
      discovered_result = discover_legacy_2025_03_26();
    }
    if (!discovered_result.has_value()) {
      fail("auth metadata discovery failed: " +
           discovered_result.error().message);
    }
    auto discovered = std::move(*discovered_result);

    if (!discovered.protected_resource.resource.empty() &&
        url_origin(discovered.protected_resource.resource) !=
            url_origin(server_url_)) {
      fail("protected resource metadata resource mismatch");
    }
    if (!discovered.authorization_server.has_value()) {
      fail("authorization server metadata was not discovered");
    }

    protected_resource_ = std::move(discovered.protected_resource);
    authorization_server_ = std::move(*discovered.authorization_server);
    validate_authorization_server_issuer();
    manager_.set_resource(protected_resource_.resource.empty()
                              ? server_url_
                              : protected_resource_.resource);
    manager_.set_authorization_server_metadata(authorization_server_);
    manager_.set_token_endpoint(
        std::shared_ptr<mcp::auth::OAuthTokenEndpoint>(
            &token_endpoint_, [](mcp::auth::OAuthTokenEndpoint*) {}));
    manager_.set_client_registration_endpoint(
        std::shared_ptr<mcp::auth::OAuthClientRegistrationEndpoint>(
            &registration_endpoint_,
            [](mcp::auth::OAuthClientRegistrationEndpoint*) {}));

    if (context_.contains("client_id") && context_["client_id"].is_string()) {
      mcp::auth::OAuthClientConfig client;
      client.client_id = context_["client_id"].get<std::string>();
      client.redirect_uri = redirect_uri_;
      if (context_.contains("client_secret") &&
          context_["client_secret"].is_string()) {
        client.client_secret = context_["client_secret"].get<std::string>();
        client.metadata["token_endpoint_auth_method"] = "client_secret_basic";
      }
      manager_.configure_client(std::move(client));
      preconfigured_client_ = true;
    } else if (scenario_ == "auth/basic-cimd" &&
               mcp::auth::supports_client_id_metadata_document(
                   authorization_server_)) {
      auto configured = unwrap(
          manager_.configure_client_id(std::string(kCimdClientMetadataUrl),
                                       redirect_uri_),
          "CIMD client configuration failed");
      (void)configured;
      preconfigured_client_ = true;
    }

    configured_ = true;
  }

  bool is_legacy_2025_03_26_scenario() const {
    return scenario_ == "auth/2025-03-26-oauth-metadata-backcompat" ||
           scenario_ == "auth/2025-03-26-oauth-endpoint-fallback";
  }

  mcp::core::Result<mcp::auth::MetadataDiscoveryResult>
  discover_legacy_2025_03_26() {
    const auto origin = url_origin(server_url_);
    mcp::auth::MetadataDiscoveryResult result;
    result.protected_resource.resource = server_url_;

    if (scenario_ == "auth/2025-03-26-oauth-metadata-backcompat") {
      auto metadata = metadata_endpoint_.fetch_authorization_server_metadata(
          mcp::auth::MetadataFetchRequest{
              origin + "/.well-known/oauth-authorization-server", {}});
      if (!metadata.has_value()) {
        return mcp::core::unexpected(metadata.error());
      }
      result.protected_resource.authorization_servers.push_back(
          metadata->issuer.empty() ? origin : metadata->issuer);
      result.authorization_server = std::move(*metadata);
      return result;
    }

    result.protected_resource.authorization_servers.push_back(origin);
    mcp::auth::AuthorizationServerMetadata metadata;
    metadata.issuer = origin;
    metadata.authorization_endpoint = origin + "/authorize";
    metadata.token_endpoint = origin + "/token";
    metadata.registration_endpoint = origin + "/register";
    metadata.response_types_supported = {"code"};
    metadata.grant_types_supported = {"authorization_code"};
    metadata.code_challenge_methods_supported = {"S256"};
    metadata.token_endpoint_auth_methods_supported = {"none"};
    result.authorization_server = std::move(metadata);
    return result;
  }

  void validate_authorization_server_issuer() const {
    if (authorization_server_.issuer.empty() ||
        protected_resource_.authorization_servers.empty()) {
      return;
    }
    const auto& servers = protected_resource_.authorization_servers;
    if (std::find(servers.begin(), servers.end(),
                  authorization_server_.issuer) == servers.end()) {
      fail("authorization server metadata issuer mismatch");
    }
  }

  bool should_rediscover(
      const mcp::auth::AuthResponseDecision& decision) const {
    return scenario_ == "auth/authorization-server-migration" &&
           decision.action ==
               mcp::auth::AuthResponseAction::kAuthorizationRequired;
  }

  mcp::auth::ScopeList select_scopes(
      const mcp::auth::AuthResponseDecision& decision) const {
    return mcp::auth::select_authorization_scopes(
        mcp::auth::ScopeSelectionContext{
            decision.required_scopes,
            protected_resource_.scopes_supported,
            authorization_server_.scopes_supported,
            {}});
  }

  mcp::core::Result<mcp::auth::TokenSet> authorize(
      const mcp::auth::AuthResponseDecision& decision) {
    const auto scopes = select_scopes(decision);
    if (preconfigured_client_) {
      auto authorization =
          unwrap(manager_.start_authorization(scopes, fixed_pkce(),
                                              next_state()),
                 "authorization URL creation failed");
      return complete_authorization(authorization.url,
                                    authorization.state.state);
    }

    mcp::auth::AuthorizationSessionRequest request;
    request.client.client_name = "cxxmcp-conformance-client";
    request.client.redirect_uri = redirect_uri_;
    request.client.scopes = scopes;
    request.client.metadata["application_type"] = "native";
    request.pkce = fixed_pkce();
    request.state = next_state();
    auto session = unwrap(manager_.start_session(std::move(request)),
                          "authorization session creation failed");
    return complete_authorization(session.authorization_url(),
                                  session.state());
  }

  mcp::core::Result<mcp::auth::TokenSet> complete_authorization(
      const std::string& authorization_url, const std::string& expected_state) {
    auto response = oauth_http_get(
        mcp::auth::MetadataFetchRequest{authorization_url, {}});
    if (!response.has_value()) {
      return mcp::core::unexpected(response.error());
    }
    const auto location = response->headers.find("Location");
    if (response->status_code < 300 || response->status_code >= 400 ||
        location == response->headers.end()) {
      return mcp::core::unexpected(mcp::auth::make_oauth_error(
          mcp::auth::OAuthErrorCode::kAuthorizationRequired,
          "authorization endpoint did not redirect with an authorization code"));
    }

    const auto params = query_params(location->second);
    const auto code = params.find("code");
    const auto state = params.find("state");
    if (code == params.end() || state == params.end()) {
      return mcp::core::unexpected(mcp::auth::make_oauth_error(
          mcp::auth::OAuthErrorCode::kAuthorizationRequired,
          "authorization redirect is missing code or state"));
    }
    if (state->second != expected_state) {
      return mcp::core::unexpected(mcp::auth::make_oauth_error(
          mcp::auth::OAuthErrorCode::kAuthorizationRequired,
          "authorization redirect state mismatch"));
    }
    const auto iss = params.find("iss");
    const auto supports_iss = authorization_server_.metadata.find(
        "authorization_response_iss_parameter_supported");
    const bool iss_required = supports_iss != authorization_server_.metadata.end() &&
                              supports_iss->second == "true";
    if (iss_required && iss == params.end()) {
      return mcp::core::unexpected(mcp::auth::make_oauth_error(
          mcp::auth::OAuthErrorCode::kAuthorizationRequired,
          "authorization redirect is missing required iss"));
    }
    if (iss != params.end() && iss->second != authorization_server_.issuer) {
      return mcp::core::unexpected(mcp::auth::make_oauth_error(
          mcp::auth::OAuthErrorCode::kAuthorizationRequired,
          "authorization redirect iss mismatch"));
    }
    return manager_.exchange_authorization_code(code->second, state->second);
  }

  mcp::core::Result<mcp::auth::TokenSet> authenticate_client_credentials() {
    mcp::auth::ClientCredentialsConfig config;
    config.client_id = context_.value("client_id", "");
    config.client_secret = context_.value("client_secret", "");
    config.resource = protected_resource_.resource.empty()
                          ? server_url_
                          : protected_resource_.resource;
    config.scopes = protected_resource_.scopes_supported;
    config.metadata["token_endpoint_auth_method"] = "client_secret_basic";
    return manager_.authenticate_client_credentials(std::move(config));
  }

  mcp::core::Result<mcp::auth::TokenSet>
  authenticate_client_credentials_jwt() {
#if defined(CXXMCP_EXAMPLES_ENABLE_AUTH_OPENSSL)
    const auto client_id = context_.value("client_id", "");
    const auto private_key_pem = context_.value("private_key_pem", "");
    const auto algorithm = context_.value("signing_algorithm", "ES256");
    auto assertion = mcp::auth::openssl::sign_private_key_jwt_assertion(
        mcp::auth::openssl::PrivateKeyJwtAssertionRequest{
            private_key_pem, algorithm, client_id,
            authorization_server_.issuer});
    if (!assertion.has_value()) {
      return mcp::core::unexpected(assertion.error());
    }

    const auto resource = protected_resource_.resource.empty()
                              ? server_url_
                              : protected_resource_.resource;
    mcp::auth::OAuthClientConfig client;
    client.client_id = client_id;

    mcp::auth::MetadataMap parameters;
    parameters["grant_type"] = "client_credentials";
    parameters["client_assertion_type"] =
        "urn:ietf:params:oauth:client-assertion-type:jwt-bearer";
    parameters["client_assertion"] = *assertion;
    parameters["resource"] = resource;
    return token_endpoint_.exchange_token_grant(
        authorization_server_, client, parameters,
        mcp::auth::OAuthErrorCode::kClientCredentialsFailed);
#else
    return mcp::core::unexpected(mcp::auth::make_oauth_error(
        mcp::auth::OAuthErrorCode::kClientCredentialsFailed,
        "private_key_jwt requires CXXMCP_AUTH_CRYPTO=OpenSSL"));
#endif
  }

  mcp::core::Result<mcp::auth::TokenSet> authenticate_enterprise_managed() {
    const auto idp_token_endpoint = context_.value("idp_token_endpoint", "");
    const auto idp_id_token = context_.value("idp_id_token", "");
    if (idp_token_endpoint.empty() || idp_id_token.empty()) {
      return mcp::core::unexpected(mcp::auth::make_oauth_error(
          mcp::auth::OAuthErrorCode::kInvalidRequest,
          "enterprise managed context is missing IdP token data"));
    }

    mcp::auth::AuthorizationServerMetadata idp_metadata;
    idp_metadata.token_endpoint = idp_token_endpoint;
    mcp::auth::OAuthClientConfig public_client;

    const auto resource = protected_resource_.resource.empty()
                              ? server_url_
                              : protected_resource_.resource;
    mcp::auth::MetadataMap token_exchange;
    token_exchange["grant_type"] =
        "urn:ietf:params:oauth:grant-type:token-exchange";
    token_exchange["subject_token"] = idp_id_token;
    token_exchange["subject_token_type"] =
        "urn:ietf:params:oauth:token-type:id_token";
    token_exchange["requested_token_type"] =
        "urn:ietf:params:oauth:token-type:id-jag";
    token_exchange["audience"] = authorization_server_.issuer;
    token_exchange["resource"] = resource;

    auto id_jag =
        token_endpoint_.exchange_token_grant(idp_metadata, public_client,
                                             token_exchange);
    if (!id_jag.has_value()) {
      return mcp::core::unexpected(id_jag.error());
    }

    mcp::auth::OAuthClientConfig client;
    client.client_id = context_.value("client_id", "");
    client.client_secret = context_.value("client_secret", "");
    client.metadata["token_endpoint_auth_method"] = "client_secret_basic";

    mcp::auth::MetadataMap jwt_bearer;
    jwt_bearer["grant_type"] =
        "urn:ietf:params:oauth:grant-type:jwt-bearer";
    jwt_bearer["assertion"] = id_jag->access_token;
    jwt_bearer["resource"] = resource;
    return token_endpoint_.exchange_token_grant(authorization_server_, client,
                                                jwt_bearer);
  }

  std::string next_state() {
    return "cxxmcp-conformance-state-" + std::to_string(++state_counter_);
  }

  std::string server_url_;
  std::string scenario_;
  Json context_;
  mcp::auth::HttpOAuthMetadataEndpoint metadata_endpoint_;
  mcp::auth::HttpOAuthTokenEndpoint token_endpoint_;
  HttpClientRegistrationEndpoint registration_endpoint_;
  mcp::auth::AuthorizationManager manager_;
  mcp::auth::ProtectedResourceMetadata protected_resource_;
  mcp::auth::AuthorizationServerMetadata authorization_server_;
  std::string redirect_uri_ = "http://127.0.0.1/cxxmcp/oauth/callback";
  bool configured_ = false;
  bool preconfigured_client_ = false;
  int state_counter_ = 0;
};

void run_auth_scenario(const std::string& server_url,
                       const std::string& scenario) {
  ConformanceAuthSession auth(server_url, scenario, conformance_context());

  auto builder = mcp::ClientPeer::builder();
  builder.streamable_http(std::string(server_url))
      .capabilities(standard_capabilities())
      .roots({mcp::protocol::Root{.uri = "file:///workspace",
                                  .name = "workspace"}})
      .auth_refresh_handler(
          [&auth](const mcp::client::HttpAuthChallenge& challenge) {
            return auth.refresh(challenge);
          });

  auto peer = unwrap(builder.build(), "client build failed");
  unwrap(peer.initialize("cxxmcp-conformance-client", "0.1.0"),
         "initialize failed");
  unwrap(peer.notify_initialized(), "initialized notification failed");
  unwrap(peer.list_tools(), "tools/list failed");
  unwrap(peer.call_tool("test-tool", Json::object()),
         "authenticated tools/call failed");
}

#else

void run_auth_scenario(const std::string&, const std::string& scenario) {
  fail("unsupported conformance client scenario: " + scenario);
}

#endif

}  // namespace

int main(int argc, char** argv) {
  try {
    const char* scenario_env = std::getenv("MCP_CONFORMANCE_SCENARIO");
    require(scenario_env != nullptr && *scenario_env != '\0',
            "MCP_CONFORMANCE_SCENARIO is required");
    require(argc >= 2, "server URL argument is required");

    const std::string scenario = scenario_env;
    const std::string server_url = argv[argc - 1];

    if (scenario == "initialize" || scenario == "json-schema-ref-no-deref") {
      run_initialize_like(server_url);
    } else if (scenario == "tools_call" || scenario == "tools-call") {
      run_tools_call(server_url);
    } else if (scenario == "elicitation-sep1034-client-defaults" ||
               scenario == "elicitation-defaults") {
      run_elicitation_defaults(server_url);
    } else if (scenario == "request-metadata") {
      run_request_metadata(server_url);
    } else if (scenario == "http-standard-headers") {
      run_http_standard_headers(server_url);
    } else if (scenario == "sep-2322-client-request-state") {
      run_mrtr_client(server_url);
    } else if (scenario == "http-custom-headers") {
      run_http_custom_headers(server_url);
    } else if (scenario == "http-invalid-tool-headers") {
      run_http_invalid_tool_headers(server_url);
    } else if (scenario == "sse-retry") {
      run_sse_retry(server_url);
    } else if (scenario.rfind("auth/", 0) == 0) {
      run_auth_scenario(server_url, scenario);
    } else {
      fail("unsupported conformance client scenario: " + scenario);
    }

    std::cout << "conformance client scenario passed: " << scenario << '\n';
    // Force exit to avoid hanging on SSE connection cleanup.
    std::fflush(nullptr);
    std::_Exit(0);
  } catch (const std::exception& ex) {
    std::cerr << "conformance everything client failed: " << ex.what() << '\n';
    return 1;
  }
}
