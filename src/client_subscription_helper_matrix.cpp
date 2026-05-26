#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "cxxmcp/client.hpp"
#include "cxxmcp/peer.hpp"
#include "cxxmcp/server.hpp"

namespace {
using Json = mcp::protocol::Json;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

class RecordingServerTransport final : public mcp::server::Transport {
public:
  mcp::core::Result<mcp::core::Unit>
  start(mcp::server::RequestHandler,
        mcp::server::NotificationHandler = {}) override {
    return mcp::core::Unit{};
  }
  mcp::core::Result<mcp::core::Unit> send_notification(
      const mcp::protocol::JsonRpcNotification &notification) override {
    notifications.push_back(notification.method);
    return mcp::core::Unit{};
  }
  void stop() noexcept override {}
  std::string_view name() const noexcept override { return "recording"; }
  std::vector<std::string> notifications;
};

class LoopbackClientTransport final : public mcp::client::Transport {
public:
  explicit LoopbackClientTransport(mcp::server::Server &server)
      : server_(server) {
    context_.session_id = "subscription-client";
    context_.remote_address = "loopback";
    context_.transport = &recording_;
  }

  mcp::core::Result<mcp::protocol::JsonRpcResponse>
  send(const mcp::protocol::JsonRpcRequest &request) override {
    return server_.handle_request(request, context_);
  }

  RecordingServerTransport &recording() { return recording_; }

private:
  mcp::server::Server &server_;
  RecordingServerTransport recording_;
  mcp::server::SessionContext context_;
};

} // namespace

int main() {
  try {
    mcp::protocol::ServerCapabilities capabilities;
    capabilities.resources.enabled = true;
    capabilities.resources.subscribe = true;
    auto built =
        mcp::server::ServerBuilder()
            .name("subscription-helper")
            .version("0.1.0")
            .with_capabilities(capabilities)
            .add_resource(mcp::protocol::Resource{.uri = "memory://watched",
                                                  .name = "watched"},
                          [](const mcp::server::ResourceContext &) {
                            mcp::protocol::ResourcesReadResult result;
                            result.contents.push_back(
                                mcp::protocol::ResourceContents{
                                    .uri = "memory://watched",
                                    .text = "watched",
                                });
                            return result;
                          })
            .build();
    require(built.has_value(), "server build failed");

    auto transport = std::make_unique<LoopbackClientTransport>(**built);
    auto *transport_ptr = transport.get();
    mcp::ClientPeer peer(mcp::client::Client(std::move(transport)));
    require(peer.subscribe("memory://watched").has_value(),
            "client subscribe failed");
    require((*built)->notify_resource_updated("memory://watched").has_value(),
            "resource update failed");
    require(transport_ptr->recording().notifications.size() == 1,
            "subscribed client did not receive update");
    require(peer.unsubscribe("memory://watched").has_value(),
            "client unsubscribe failed");
    require((*built)->notify_resource_updated("memory://watched").has_value(),
            "resource update after unsubscribe failed");
    require(transport_ptr->recording().notifications.size() == 1,
            "unsubscribed client received update");
    std::cout << "client subscription helper matrix passed\n";
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "client subscription helper matrix failed: " << ex.what()
              << '\n';
    return 1;
  }
}
