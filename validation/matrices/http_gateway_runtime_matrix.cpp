#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>

#include "cxxmcp/client.hpp"
#include "cxxmcp/gateway.hpp"

#ifndef CXXMCP_EXAMPLES_CHILD_EXE
#define CXXMCP_EXAMPLES_CHILD_EXE ""
#endif

namespace {
using Json = mcp::protocol::Json;
constexpr int kPort = 39987;
constexpr std::string_view kPath = "/cxxmcp/examples";

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

bool wait_for_gateway() {
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
    require(std::string_view(CXXMCP_EXAMPLES_CHILD_EXE).size() != 0,
            "child server executable is not configured");

    auto runtime =
        mcp::gateway::Runtime::builder()
            .profile("examples.gateway")
            .host("127.0.0.1")
            .port(kPort)
            .path(std::string(kPath))
            .instruction("Expose the downstream example server over HTTP.")
            .trust(true)
            .discover(true)
            .bind_server("minimal.stdio")
            .add_stdio_server("minimal.stdio", CXXMCP_EXAMPLES_CHILD_EXE, {})
            .build();
    require(runtime.has_value(), "gateway runtime build failed");
    require(runtime->start().has_value(), "gateway runtime start failed");
    require(wait_for_gateway(), "gateway did not become reachable");

    auto client = mcp::client::Client::connect_streamable_http(
        mcp::client::Client::StreamableHttpEndpoint{
            .host = "127.0.0.1",
            .port = kPort,
            .path = std::string(kPath),
            .timeout = std::chrono::seconds(2),
        });
    require(client.initialize("gateway-probe", "0.1.0").has_value(),
            "gateway initialize failed");
    require(client.notify_initialized().has_value(),
            "gateway initialized notification failed");

    const auto tools = client.list_tools();
    require(tools.has_value() && !tools->empty(), "gateway tools/list failed");
    const auto call =
        client.call_raw(tools->front().name, Json{{"text", "via-http"}});
    require(call.has_value(), "gateway tools/call failed");

    const auto prompts = client.list_prompts();
    require(prompts.has_value() && !prompts->empty(),
            "gateway prompts/list failed");
    const auto prompt =
        client.get_prompt(prompts->front().name, Json{{"name", "sdk"}});
    require(prompt.has_value() && !prompt->messages.empty(),
            "gateway prompts/get failed");

    const auto resources = client.list_resources();
    require(resources.has_value() && !resources->empty(),
            "gateway resources/list failed");
    const auto resource = client.read_resource(resources->front().uri);
    require(resource.has_value() && !resource->contents.empty(),
            "gateway resources/read failed");

    require(runtime->stop().has_value(), "gateway runtime stop failed");
    std::cout << "http gateway runtime matrix passed\n";
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "http gateway runtime matrix failed: " << ex.what() << '\n';
    return 1;
  }
}
