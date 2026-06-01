#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include "cxxmcp/peer.hpp"
#include "cxxmcp/service.hpp"
#include "cxxmcp/transport/process_stdio_transport.hpp"

#ifndef CXXMCP_EXAMPLES_CHILD_EXE
#define CXXMCP_EXAMPLES_CHILD_EXE ""
#endif

namespace {
using Json = mcp::protocol::Json;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

} // namespace

int main() {
  try {
    require(std::string_view(CXXMCP_EXAMPLES_CHILD_EXE).size() != 0,
            "child server executable is not configured");

    auto transport =
        std::make_unique<mcp::transport::ProcessStdioClientTransport>(
            mcp::transport::ProcessStdioClientTransportOptions{
                .command = CXXMCP_EXAMPLES_CHILD_EXE,
                .request_timeout = std::chrono::seconds(5),
            });
    require(transport->name() == "process-stdio",
            "process transport name mismatch");
    require(transport->diagnostics().is_object(),
            "process transport diagnostics missing");

    auto running = mcp::serve(mcp::ClientPeer(std::move(transport)));
    require(running.has_value() && running->running(),
            "process stdio service failed to start");

    require(
        running->peer().initialize("cxxmcp-process-probe", "0.1.0").has_value(),
        "initialize failed");
    require(running->peer().notify_initialized().has_value(),
            "initialized notification failed");

    const auto tools = running->peer().list_tools();
    require(tools.has_value() && !tools->empty(), "tools/list failed");

    const auto call =
        running->peer().call_tool("echo", Json{{"text", "from-process-stdio"}});
    require(call.has_value(), "tools/call failed");

    const auto prompts = running->peer().list_prompts();
    require(prompts.has_value() && !prompts->empty(), "prompts/list failed");

    const auto resources = running->peer().list_resources();
    require(resources.has_value() && !resources->empty(),
            "resources/list failed");

    const auto health =
        running->peer().raw_request(mcp::protocol::JsonRpcRequest{
            .method = "example/health",
            .params = Json::object(),
            .id = std::int64_t{77},
        });
    require(health.has_value() && health->value("ok", false),
            "raw health failed");

    require(running->stop().has_value(), "process stdio service stop failed");

    auto compat_peer =
        mcp::ClientPeer::connect_stdio(mcp::client::Client::StdioEndpoint{
            .command = CXXMCP_EXAMPLES_CHILD_EXE});
    require(compat_peer.initialize("cxxmcp-connect-stdio", "0.1.0").has_value(),
            "connect_stdio initialize failed");
    require(compat_peer.notify_initialized().has_value(),
            "connect_stdio initialized notification failed");
    const auto compat_tools = compat_peer.list_tools();
    require(compat_tools.has_value() && !compat_tools->empty(),
            "connect_stdio tools/list failed");
    compat_peer.stop();

    std::cout << "process stdio client probe passed\n";
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "process stdio client probe failed: " << ex.what() << '\n';
    return 1;
  }
}
