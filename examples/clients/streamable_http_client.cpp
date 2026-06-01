#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "cxxmcp/client.hpp"
#include "cxxmcp/peer.hpp"
#include "cxxmcp/service.hpp"

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

} // namespace

int main(int argc, char **argv) {
  try {
    mcp::client::Client::StreamableHttpEndpoint endpoint;
    endpoint.uri = argc > 1 ? argv[1] : "http://127.0.0.1:3000/mcp";

    auto running = mcp::serve(
        mcp::ClientPeer::connect_streamable_http(std::move(endpoint)));
    require(running.has_value(), "streamable HTTP service failed to start");

    require(
        running->peer().initialize("cxxmcp-http-client", "0.1.0").has_value(),
        "streamable HTTP initialize failed");
    require(running->peer().notify_initialized().has_value(),
            "initialized notification failed");

    const auto tools = running->peer().list_tools();
    require(tools.has_value(), "streamable HTTP tools/list failed");

    std::cout << "streamable HTTP client connected; tools=" << tools->size()
              << '\n';
    require(running->stop().has_value(),
            "streamable HTTP service failed to stop");
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "streamable HTTP client example failed: " << ex.what() << '\n';
    return 1;
  }
}
