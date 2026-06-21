#include <cstdlib>
#include <iostream>
#include <string>

#include "cxxmcp/peer.hpp"
#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/run.hpp"

namespace {

using Json = mcp::protocol::Json;

struct ShoutArgs {
  std::string text;

  CXXMCP_REFLECT_SELF(ShoutArgs, text)
};

struct ShoutResult {
  std::string text;

  CXXMCP_REFLECT_SELF(ShoutResult, text)
};

int configured_port(int argc, char** argv) {
  if (argc > 1) {
    return std::stoi(argv[1]);
  }
#if defined(_WIN32)
  char* value = nullptr;
  std::size_t size = 0;
  if (_dupenv_s(&value, &size, "PORT") == 0 && value != nullptr) {
    const int port = std::stoi(value);
    std::free(value);
    return port;
  }
  if (_dupenv_s(&value, &size, "CXXMCP_TYPED_AUTHORING_PORT") == 0 &&
      value != nullptr) {
    const int port = std::stoi(value);
    std::free(value);
    return port;
  }
#else
  if (const char* value = std::getenv("PORT")) {
    return std::stoi(value);
  }
  if (const char* value = std::getenv("CXXMCP_TYPED_AUTHORING_PORT")) {
    return std::stoi(value);
  }
#endif
  return 3000;
}

}  // namespace

int main(int argc, char** argv) {
  const int port = configured_port(argc, argv);
  std::cerr << "cxxmcp typed authoring conformance server listening on "
            << "http://127.0.0.1:" << port << "/mcp\n";

  return mcp::ServerPeer::builder()
      .name("cxxmcp-typed-authoring-conformance-server")
      .version("1.0.0")
      .instructions("C++ MCP typed authoring server for conformance testing.")
      .streamable_http("127.0.0.1", port, "/mcp")
      .tool(mcp::server::tool<Json, Json>("echo")
                .description("Echoes an object payload through the typed API.")
                .handler([](const Json& input) {
                  return Json{{"echo", input}};
                }))
      .tool(mcp::server::tool<std::string, std::string>("shout_scalar")
                .description("Adds an exclamation mark to scalar text.")
                .handler([](std::string text) { return text + "!"; }))
      .tool(mcp::server::tool<ShoutArgs, ShoutResult>("shout_object")
                .description("Adds an exclamation mark to reflected text.")
                .handler([](ShoutArgs args) {
                  return ShoutResult{.text = args.text + "!"};
                }))
      .run();
}
