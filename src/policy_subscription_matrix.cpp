#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cxxmcp/error.hpp"
#include "cxxmcp/server.hpp"
#include "cxxmcp/server/auth.hpp"
#include "cxxmcp/server/rate_limit.hpp"

namespace {
using Json = mcp::protocol::Json;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

class RecordingTransport final : public mcp::server::Transport {
public:
  mcp::core::Result<mcp::core::Unit>
  start(mcp::server::RequestHandler,
        mcp::server::NotificationHandler = {}) override {
    return mcp::core::Unit{};
  }

  mcp::core::Result<mcp::core::Unit> send_notification(
      const mcp::protocol::JsonRpcNotification &notification) override {
    notifications.push_back(notification);
    return mcp::core::Unit{};
  }

  void stop() noexcept override {}
  std::string_view name() const noexcept override { return "recording"; }

  std::vector<mcp::protocol::JsonRpcNotification> notifications;
};

class AllowAuth final : public mcp::server::AuthProvider {
public:
  explicit AllowAuth(int &calls) : calls_(calls) {}

  mcp::core::Result<mcp::server::AuthIdentity>
  authenticate(const mcp::server::AuthRequest &request) override {
    ++calls_;
    require(request.remote_address == "policy-loopback",
            "auth remote address mismatch");
    return mcp::server::AuthIdentity{.subject = "example-user",
                                     .claims = {{"role", "tester"}}};
  }

private:
  int &calls_;
};

class RejectAuth final : public mcp::server::AuthProvider {
public:
  mcp::core::Result<mcp::server::AuthIdentity>
  authenticate(const mcp::server::AuthRequest &) override {
    return mcp::core::unexpected(mcp::errors::make(
        mcp::protocol::ErrorCode::PermissionDenied, "token rejected"));
  }
};

class MethodRateLimiter final : public mcp::server::RateLimiter {
public:
  explicit MethodRateLimiter(int &calls) : calls_(calls) {}

  mcp::core::Result<mcp::server::RateLimitDecision>
  check(const mcp::server::RateLimitRequest &request) override {
    ++calls_;
    require(!request.method.empty(), "rate limit method missing");
    if (request.method == "example/rate_limited") {
      return mcp::server::RateLimitDecision{
          .allowed = false,
          .retry_after = std::chrono::milliseconds(50),
      };
    }
    return mcp::server::RateLimitDecision{.allowed = true};
  }

private:
  int &calls_;
};

std::unique_ptr<mcp::server::Server> make_policy_server(int &auth_calls,
                                                        int &rate_calls) {
  mcp::protocol::ServerCapabilities capabilities;
  capabilities.resources.enabled = true;
  capabilities.resources.subscribe = true;

  auto built =
      mcp::server::ServerBuilder()
          .name("cxxmcp-policy-subscription")
          .version("0.1.0")
          .with_capabilities(capabilities)
          .with_auth_provider(std::make_unique<AllowAuth>(auth_calls))
          .with_rate_limiter(std::make_unique<MethodRateLimiter>(rate_calls))
          .add_resource(mcp::protocol::Resource{.uri = "memory://watched",
                                                .name = "Watched resource"},
                        [](const mcp::server::ResourceContext &) {
                          mcp::protocol::ResourcesReadResult result;
                          result.contents.push_back(
                              mcp::protocol::ResourceContents{
                                  .uri = "memory://watched",
                                  .text = "watch me",
                              });
                          return result;
                        })
          .build();
  require(built.has_value(), "policy server build failed");
  return std::move(*built);
}

} // namespace

int main() {
  try {
    int auth_calls = 0;
    int rate_calls = 0;
    auto server = make_policy_server(auth_calls, rate_calls);
    RecordingTransport transport;
    mcp::server::SessionContext context{.session_id = "policy-session",
                                        .remote_address = "policy-loopback",
                                        .transport = &transport};

    auto init = server->handle_request(
        mcp::protocol::JsonRpcRequest{
            .method = std::string(mcp::protocol::InitializeMethod),
            .params = Json{{"protocolVersion",
                            std::string(mcp::protocol::McpProtocolVersion)}},
            .id = std::int64_t{1},
        },
        context);
    require(init.has_value() && init->result.has_value(),
            "initialize with auth/rate failed");
    require(init->result->at("capabilities").at("resources").at("subscribe") ==
                true,
            "resource subscribe capability missing");

    auto subscribed = server->handle_request(
        mcp::protocol::JsonRpcRequest{
            .method = std::string(mcp::protocol::ResourcesSubscribeMethod),
            .params = Json{{"uri", "memory://watched"}},
            .id = std::int64_t{2}},
        context);
    require(subscribed.has_value() && subscribed->result.has_value(),
            "resources/subscribe failed");

    require(server->notify_resource_updated("memory://watched").has_value(),
            "resource update notification failed");
    require(transport.notifications.size() == 1,
            "subscribed transport did not receive update");
    require(transport.notifications.front().method ==
                mcp::protocol::ResourcesUpdatedNotificationMethod,
            "resource update method mismatch");

    auto unsubscribed = server->handle_request(
        mcp::protocol::JsonRpcRequest{
            .method = std::string(mcp::protocol::ResourcesUnsubscribeMethod),
            .params = Json{{"uri", "memory://watched"}},
            .id = std::int64_t{3}},
        context);
    require(unsubscribed.has_value() && unsubscribed->result.has_value(),
            "resources/unsubscribe failed");
    require(server->notify_resource_updated("memory://watched").has_value(),
            "resource update after unsubscribe failed");
    require(transport.notifications.size() == 1,
            "unsubscribed transport received update");

    auto limited = server->handle_request(
        mcp::protocol::JsonRpcRequest{.method = "example/rate_limited",
                                      .params = Json::object(),
                                      .id = std::int64_t{4}},
        context);
    require(limited.has_value() && limited->error.has_value(),
            "rate limited request should return an error response");
    require(limited->error->code ==
                static_cast<int>(mcp::protocol::ErrorCode::RateLimited),
            "rate limited error code mismatch");

    auto rejecting = mcp::server::ServerBuilder()
                         .name("rejecting")
                         .version("0.1.0")
                         .with_auth_provider(std::make_unique<RejectAuth>())
                         .build();
    require(rejecting.has_value(), "rejecting server build failed");
    auto denied = (*rejecting)
                      ->handle_request(
                          mcp::protocol::JsonRpcRequest{
                              .method = std::string(mcp::protocol::PingMethod),
                              .params = Json::object(),
                              .id = std::int64_t{5}},
                          context);
    require(denied.has_value() && denied->error.has_value(),
            "auth denied request should return an error response");
    require(denied->error->code ==
                static_cast<int>(mcp::protocol::ErrorCode::PermissionDenied),
            "auth denied error code mismatch");
    require(auth_calls >= 4 && rate_calls >= 4,
            "auth/rate hooks were not exercised");

    std::cout << "policy subscription matrix passed\n";
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "policy subscription matrix failed: " << ex.what() << '\n';
    return 1;
  }
}
