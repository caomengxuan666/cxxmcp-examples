#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <thread>

#include "cxxmcp/client.hpp"
#include "cxxmcp/server.hpp"

namespace {
using Json = mcp::protocol::Json;
constexpr int kPort = 39988;
constexpr std::string_view kPath = "/cxxmcp/direct";

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

bool wait_for_http() {
  for (int attempt = 0; attempt < 50; ++attempt) {
    auto client = mcp::client::Client::connect_streamable_http(
        mcp::client::Client::StreamableHttpEndpoint{
            .host = "127.0.0.1",
            .port = kPort,
            .path = std::string(kPath),
            .timeout = std::chrono::milliseconds(250),
        });
    if (client.ping().has_value()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return false;
}

} // namespace

int main() {
  try {
    auto built = mcp::server::App::builder()
                     .name("direct-http")
                     .version("0.1.0")
                     .streamable_http("127.0.0.1", kPort, std::string(kPath))
                     .tool(mcp::server::tool<Json, Json>("echo").handler(
                         [](const Json &args) { return args; }))
                     .build();
    require(built.has_value(), "server build failed");
    auto server = std::move(*built);
    std::thread server_thread([&] { (void)server->start(); });
    require(wait_for_http(), "direct http server did not start");

    auto client = mcp::client::Client::connect_streamable_http(
        mcp::client::Client::StreamableHttpEndpoint{
            .host = "127.0.0.1",
            .port = kPort,
            .path = std::string(kPath),
            .auth_header = "example-token",
            .timeout = std::chrono::seconds(2),
        });
    require(client.initialize("direct-http", "0.1.0").has_value(),
            "http initialize failed");
    const auto tools = client.list_tools();
    require(tools.has_value() && tools->size() == 1, "http tools/list failed");
    const auto call = client.call_raw("echo", Json{{"value", "http"}});
    require(call.has_value(), "http tools/call failed");

    auto legacy = mcp::client::Client::connect_legacy_sse(
        mcp::client::Client::StreamableHttpEndpoint{
            .host = "127.0.0.1",
            .port = kPort,
            .path = std::string(kPath),
            .timeout = std::chrono::seconds(2),
        });
    require(legacy.initialize("legacy-sse", "0.1.0").has_value(),
            "legacy sse initialize failed");
    require(legacy.list_tools().has_value(), "legacy sse tools/list failed");

    server->stop();
    if (server_thread.joinable()) {
      server_thread.join();
    }
    std::cout << "direct http legacy sse matrix passed\n";
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "direct http legacy sse matrix failed: " << ex.what() << '\n';
    return 1;
  }
}
