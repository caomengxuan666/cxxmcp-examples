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

class NoopTransport final : public mcp::client::Transport {
public:
  mcp::core::Result<mcp::protocol::JsonRpcResponse>
  send(const mcp::protocol::JsonRpcRequest &request) override {
    return mcp::protocol::make_response(request.id, Json{{"completed", true}});
  }
};

} // namespace

int main() {
  try {
    mcp::ClientPeer peer(mcp::client::Client(std::make_unique<NoopTransport>()));

    require(peer.initialize("timeout-demo", "0.1.0").has_value(),
            "initialize failed");

    auto result = peer.call_tool("test.tool", Json::object());
    require(result.has_value(), "synchronous call failed");
    require(result->structured_content.has_value() &&
                (*result->structured_content)["completed"] == true,
            "result mismatch");

    std::cout << "timeout cancellation client example passed\n";
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "timeout cancellation client example failed: " << ex.what()
              << '\n';
    return 1;
  }
}
