#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "cxxmcp/cancellation.hpp"
#include "cxxmcp/client.hpp"
#include "cxxmcp/peer.hpp"
#include "cxxmcp/request.hpp"
#include "cxxmcp/server.hpp"

namespace {

using Json = mcp::protocol::Json;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

class LoopbackTransport final : public mcp::client::Transport {
public:
  explicit LoopbackTransport(mcp::server::Server &server) : server_(server) {}

  mcp::core::Result<mcp::protocol::JsonRpcResponse>
  send(const mcp::protocol::JsonRpcRequest &request) override {
    if (request.method == "demo/slow") {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return server_.handle_request(request, context_);
  }

  mcp::core::Result<mcp::core::Unit> send_notification(
      const mcp::protocol::JsonRpcNotification &notification) override {
    std::lock_guard lock(mutex_);
    notifications_.push_back(notification.method);
    return mcp::core::Unit{};
  }

  std::size_t notification_count(std::string_view method) const {
    std::lock_guard lock(mutex_);
    std::size_t count = 0;
    for (const auto &notification : notifications_) {
      if (notification == method) {
        ++count;
      }
    }
    return count;
  }

private:
  mcp::server::Server &server_;
  mcp::server::SessionContext context_{.session_id = "timeout-demo",
                                       .remote_address = "loopback"};
  mutable std::mutex mutex_;
  std::vector<std::string> notifications_;
};

} // namespace

int main() {
  try {
    const auto configured = mcp::configure_request_executor(
        mcp::RequestExecutorOptions{.worker_count = 2, .max_queue_size = 8});
    require(configured.has_value(), "request executor configure failed");

    auto built =
        mcp::server::App::builder()
            .name("timeout-cancellation-demo")
            .version("0.1.0")
            .raw_request([](const mcp::protocol::JsonRpcRequest &request)
                             -> std::optional<mcp::protocol::JsonRpcResponse> {
              if (request.method == "demo/slow") {
                return mcp::protocol::make_response(request.id,
                                                    Json{{"completed", true}});
              }
              return std::nullopt;
            })
            .build();
    require(built.has_value(), "server build failed");

    auto transport = std::make_unique<LoopbackTransport>(**built);
    auto *transport_ptr = transport.get();
    mcp::ClientPeer peer(mcp::client::Client(std::move(transport)));

    require(peer.initialize("timeout-demo", "0.1.0").has_value(),
            "initialize failed");

    mcp::RequestOptions timeout_options;
    timeout_options.timeout = std::chrono::milliseconds(10);
    auto timed_out =
        peer.request_async("demo/slow", Json::object(), timeout_options)
            .await_response();
    require(!timed_out.has_value(), "timed-out request unexpectedly succeeded");
    require(transport_ptr->notification_count(
                mcp::protocol::CancelledNotificationMethod) >= 1,
            "timeout should emit notifications/cancelled");

    mcp::CancellationSource cancellation;
    mcp::RequestOptions cancel_options;
    cancel_options.cancellation_token = cancellation.token();
    auto handle =
        peer.request_async("demo/slow", Json::object(), cancel_options);
    cancellation.cancel();

    auto cancelled = handle.await_response();
    require(!cancelled.has_value(), "cancelled request unexpectedly succeeded");
    require(handle.cancel("already cancelled").has_value(),
            "idempotent request cancel failed");

    std::cout << "timeout cancellation client example passed\n";
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "timeout cancellation client example failed: " << ex.what()
              << '\n';
    return 1;
  }
}
