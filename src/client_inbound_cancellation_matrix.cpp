#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#include "cxxmcp/cancellation.hpp"
#include "cxxmcp/client.hpp"
#include "cxxmcp/peer.hpp"

namespace {
using Json = mcp::protocol::Json;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

class NoopTransport final : public mcp::client::Transport {
 public:
  mcp::core::Result<mcp::protocol::JsonRpcResponse> send(
      const mcp::protocol::JsonRpcRequest& request) override {
    return mcp::protocol::make_response(request.id, Json::object());
  }
};

const mcp::protocol::JsonRpcResponse& response_from(
    const mcp::core::Result<std::optional<mcp::protocol::JsonRpcMessage>>&
        dispatched) {
  require(dispatched.has_value(), "dispatch failed");
  require(dispatched->has_value(), "dispatch did not return a response");
  const auto* response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&**dispatched);
  require(response != nullptr, "dispatch returned the wrong message kind");
  require(response->result.has_value(), "dispatch response is not successful");
  return *response;
}

}  // namespace

int main() {
  try {
    auto transport = std::make_unique<NoopTransport>();
    mcp::ClientPeer peer(mcp::client::Client(std::move(transport)));

    std::atomic_bool handler_started{false};
    peer.on_custom_request(
        [&](const mcp::protocol::JsonRpcRequest& request)
            -> mcp::core::Result<Json> {
          require(request.method == "client/slow",
                  "custom request method mismatch");
          handler_started.store(true);
          std::this_thread::sleep_for(std::chrono::milliseconds(500));
          return Json{{"completed", true}};
        });

    auto pending = std::async(std::launch::async, [&] {
      return peer.dispatch_message(mcp::protocol::JsonRpcRequest{
          .method = "client/slow",
          .params = Json::object(),
          .id = std::int64_t{42},
      });
    });

    for (int attempt = 0; attempt < 100 && !handler_started.load(); ++attempt) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(handler_started.load(), "custom request handler did not start");

    require(peer
                .dispatch_message(mcp::protocol::JsonRpcNotification{
                    .method =
                        std::string(mcp::protocol::CancelledNotificationMethod),
                    .params =
                        Json{{"requestId", 42}, {"reason", "caller cancelled"}},
                })
                .has_value(),
            "cancel notification dispatch failed");

    const auto dispatched = pending.get();
    const auto& response = response_from(dispatched);
    require(response.result->at("completed") == true,
            "custom request did not complete");

    std::cout << "client inbound cancellation matrix passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "client inbound cancellation matrix failed: " << ex.what()
              << '\n';
    return 1;
  }
}
