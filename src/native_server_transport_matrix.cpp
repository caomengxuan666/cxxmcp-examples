#include <cstdint>
#include <deque>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <variant>
#include <vector>

#include "cxxmcp/peer.hpp"
#include "cxxmcp/server.hpp"
#include "cxxmcp/transport.hpp"

namespace {
using Json = mcp::protocol::Json;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

class ScriptedServerTransport final : public mcp::transport::ServerTransport {
public:
  std::string_view name() const noexcept override { return "scripted-server"; }

  mcp::protocol::Json diagnostics() const override {
    return Json{{"name", "scripted-server"}, {"sent", sent.size()}};
  }

  mcp::core::Result<mcp::core::Unit> send(TxMessage message) override {
    sent.push_back(std::move(message));
    return mcp::core::Unit{};
  }

  mcp::core::Result<std::optional<RxMessage>> receive() override {
    if (incoming.empty()) {
      return std::nullopt;
    }
    auto message = std::move(incoming.front());
    incoming.pop_front();
    return message;
  }

  mcp::core::Result<mcp::core::Unit> close() override {
    closed = true;
    return mcp::core::Unit{};
  }

  void push(mcp::protocol::JsonRpcRequest request) {
    incoming.push_back(mcp::protocol::JsonRpcMessage{std::move(request)});
  }

  std::deque<RxMessage> incoming;
  std::vector<TxMessage> sent;
  bool closed = false;
};

const mcp::protocol::JsonRpcResponse &
response_at(const ScriptedServerTransport &transport, std::size_t index) {
  require(index < transport.sent.size(), "missing transport response");
  const auto *response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&transport.sent.at(index));
  require(response != nullptr, "sent message is not a response");
  return *response;
}

} // namespace

int main() {
  try {
    auto server = mcp::server::App::builder()
                      .name("native-transport-server")
                      .version("0.1.0")
                      .tool(mcp::server::tool<Json, Json>("echo").handler(
                          [](const Json &args) { return args; }))
                      .build();
    require(server.has_value(), "server build failed");

    mcp::ServerPeer peer(std::move(*server));
    ScriptedServerTransport transport;
    transport.push(mcp::protocol::JsonRpcRequest{
        .method = std::string(mcp::protocol::InitializeMethod),
        .params = Json{{"protocolVersion",
                        std::string(mcp::protocol::McpProtocolVersion)}},
        .id = std::int64_t{1},
    });
    transport.push(mcp::protocol::JsonRpcRequest{
        .method = std::string(mcp::protocol::ToolsListMethod),
        .params = Json::object(),
        .id = std::int64_t{2},
    });
    transport.push(mcp::protocol::JsonRpcRequest{
        .method = std::string(mcp::protocol::ToolsCallMethod),
        .params = mcp::protocol::tool_call_to_json(mcp::protocol::ToolCall{
            .name = "echo",
            .arguments = Json{{"value", 7}},
        }),
        .id = std::int64_t{3},
    });

    require(peer.serve_transport(transport).has_value(),
            "serve_transport failed");
    require(transport.diagnostics().at("sent") == 3,
            "transport diagnostics mismatch");
    require(response_at(transport, 0).result->contains("serverInfo"),
            "initialize response mismatch");
    require(response_at(transport, 1).result->at("tools").size() == 1,
            "tools/list response mismatch");
    require(
        response_at(transport, 2).result->at("structuredContent").at("value") ==
            7,
        "tools/call response mismatch");

    std::cout << "native server transport matrix passed\n";
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "native server transport matrix failed: " << ex.what() << '\n';
    return 1;
  }
}
