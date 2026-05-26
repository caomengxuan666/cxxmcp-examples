#include <chrono>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "cxxmcp/peer.hpp"
#include "cxxmcp/server.hpp"
#include "cxxmcp/server/auth.hpp"
#include "cxxmcp/service.hpp"

namespace {
using Json = mcp::protocol::Json;
constexpr int kPort = 39989;
constexpr std::string_view kPath = "/cxxmcp/auth";

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

class BearerAuth final : public mcp::server::AuthProvider {
 public:
  mcp::core::Result<mcp::server::AuthIdentity> authenticate(
      const mcp::server::AuthRequest& request) override {
    const auto authorization = request.headers.find("Authorization");
    if (authorization == request.headers.end() ||
        authorization->second != "Bearer valid-token") {
      return std::unexpected(mcp::server::make_auth_error(
          "authentication failed", "expected Bearer valid-token"));
    }

    return mcp::server::AuthIdentity{
        .subject = "example-http-user",
        .claims = {{"scope", "tools:call"}},
    };
  }
};

mcp::ClientPeer make_http_peer(std::string token = {}) {
  mcp::client::Client::StreamableHttpEndpoint endpoint;
  endpoint.host = "127.0.0.1";
  endpoint.port = kPort;
  endpoint.path = std::string(kPath);
  endpoint.timeout = std::chrono::seconds(2);
  endpoint.headers.emplace("X-Example-Client", "cxxmcp-auth-lite");
  if (!token.empty()) {
    endpoint.auth_header = std::move(token);
  }
  return mcp::ClientPeer::connect_streamable_http(std::move(endpoint));
}

bool wait_for_http() {
  for (int attempt = 0; attempt < 50; ++attempt) {
    auto running = mcp::serve(make_http_peer("valid-token"));
    if (running.has_value()) {
      if (running->peer().initialize("auth-wait", "0.1.0").has_value()) {
        (void)running->stop();
        return true;
      }
      (void)running->stop();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return false;
}

}  // namespace

int main() {
  std::optional<mcp::RunningService<mcp::RoleServer>> running_server;

  try {
    mcp::server::HttpTransportOptions http_options;
    http_options.listen_host = "127.0.0.1";
    http_options.listen_port = kPort;
    http_options.path = std::string(kPath);
    http_options.auth_challenge = "Bearer realm=\"cxxmcp-examples\"";

    auto built =
        mcp::server::ServerBuilder()
            .name("http-auth-lite")
            .version("0.1.0")
            .with_transport(std::make_unique<mcp::server::HttpTransport>(
                std::move(http_options)))
            .with_auth_provider(std::make_unique<BearerAuth>())
            .add_tool(
                mcp::protocol::ToolDefinition{
                    .name = "whoami",
                    .description = "Return the authenticated subject.",
                    .input_schema = Json::object(),
                },
                [](const mcp::server::ToolContext& context)
                    -> mcp::core::Result<mcp::protocol::ToolResult> {
                  require(context.auth_identity.has_value(),
                          "auth identity should reach tool context");
                  require(
                      context.auth_identity->claims.at("scope") == "tools:call",
                      "auth claim mismatch");
                  return mcp::protocol::ToolResult::text(
                      context.auth_identity->subject);
                })
            .build();
    require(built.has_value(), "server build failed");

    auto served = mcp::serve(mcp::ServerPeer(std::move(*built)));
    require(served.has_value(), "server service failed to start");
    running_server.emplace(std::move(*served));
    require(wait_for_http(), "authorized http server did not start");

    auto denied_service = mcp::serve(make_http_peer());
    require(denied_service.has_value(), "unauthorized client service failed");
    auto denied =
        denied_service->peer().initialize("unauthorized", "0.1.0");
    require(!denied.has_value(), "unauthorized initialize should fail");
    require(denied.error().category == "transport",
            "unauthorized initialize should be a transport error");
    require(denied.error().detail == "401",
            "unauthorized initialize should return HTTP 401");
    require(denied_service->stop().has_value(),
            "unauthorized client service stop failed");

    auto client = mcp::serve(make_http_peer("valid-token"));
    require(client.has_value(), "authorized client service failed");
    require(client->peer().initialize("authorized", "0.1.0").has_value(),
            "authorized initialize failed");
    require(client->peer().notify_initialized().has_value(),
            "authorized initialized notification failed");
    const auto tools = client->peer().list_tools();
    require(tools.has_value() && tools->size() == 1,
            "authorized tools/list failed");
    const auto whoami = client->peer().call_tool("whoami", Json::object());
    require(whoami.has_value(), "authorized whoami call failed");
    require(!whoami->content.empty(), "authorized whoami content missing");
    require(whoami->content.front().text == "example-http-user",
            "authorized whoami subject mismatch");
    require(client->stop().has_value(), "authorized client service stop failed");

    require(running_server->stop().has_value(),
            "server service stop failed");

    std::cout << "http auth lite matrix passed\n";
    return 0;
  } catch (const std::exception& ex) {
    if (running_server.has_value()) {
      (void)running_server->stop();
    }
    std::cerr << "http auth lite matrix failed: " << ex.what() << '\n';
    return 1;
  }
}
