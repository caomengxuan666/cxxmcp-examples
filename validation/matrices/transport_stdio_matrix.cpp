#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string_view>

#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/transport/stdio_transport.hpp"

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

const mcp::protocol::JsonRpcRequest &
as_request(const mcp::protocol::JsonRpcMessage &message) {
  const auto *request = std::get_if<mcp::protocol::JsonRpcRequest>(&message);
  require(request != nullptr, "message is not a request");
  return *request;
}

const mcp::protocol::JsonRpcResponse &
as_response(const mcp::protocol::JsonRpcMessage &message) {
  const auto *response = std::get_if<mcp::protocol::JsonRpcResponse>(&message);
  require(response != nullptr, "message is not a response");
  return *response;
}

} // namespace

int main() {
  try {
    std::stringstream client_to_server;
    std::stringstream server_to_client;

    mcp::transport::ClientStdioTransport client(server_to_client,
                                                client_to_server);
    mcp::transport::ServerStdioTransport server(client_to_server,
                                                server_to_client);

    require(client.name() == "stdio", "client transport name mismatch");
    require(server.name() == "stdio", "server transport name mismatch");
    require(client.diagnostics().at("closed") == false,
            "client diagnostics mismatch");

    auto sent =
        client.send(mcp::protocol::JsonRpcMessage{mcp::protocol::JsonRpcRequest{
            .method = "ping",
            .params = mcp::protocol::Json::object(),
            .id = std::int64_t{1},
        }});
    require(sent.has_value(), "client send failed");

    auto inbound = server.receive();
    require(inbound.has_value() && inbound->has_value(),
            "server receive failed");
    const auto &request = as_request(**inbound);
    require(request.method == "ping", "server request method mismatch");

    sent = server.send(
        mcp::protocol::JsonRpcMessage{mcp::protocol::JsonRpcResponse{
            .id = request.id,
            .result = mcp::protocol::Json{{"ok", true}},
        }});
    require(sent.has_value(), "server send failed");

    auto outbound = client.receive();
    require(outbound.has_value() && outbound->has_value(),
            "client receive failed");
    const auto &response = as_response(**outbound);
    require(response.result.has_value() && response.result->at("ok") == true,
            "client response payload mismatch");

    require(client.close().has_value(), "client close failed");
    require(client.diagnostics().at("closed") == true,
            "client close diagnostics mismatch");

    std::cout << "transport stdio matrix passed\n";
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "transport stdio matrix failed: " << ex.what() << '\n';
    return 1;
  }
}
