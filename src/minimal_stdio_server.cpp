#include <optional>
#include <string>

#include "cxxmcp/peer.hpp"
#include "cxxmcp/run.hpp"
#include "cxxmcp/server.hpp"

int main() {
  using Json = mcp::protocol::Json;

  return mcp::ServerPeer::builder()
      .name("cxxmcp-minimal")
      .version("0.1.0")
      .instructions("Smallest useful stdio server: one tool, prompt, resource, and health request.")
      .stdio()
      .tool<Json, Json>("echo", [](const Json& args) {
        return Json{{"echo", args}, {"ok", true}};
      })
      .prompt("hello.prompt", [](const mcp::server::PromptContext& context) {
        const auto name = context.arguments.value("name", std::string{"world"});
        mcp::protocol::PromptsGetResult result;
        result.messages.push_back(mcp::protocol::PromptMessage{
            .role = "user",
            .content = mcp::protocol::ContentBlock::text_content("Say hello to " + name),
        });
        return result;
      })
      .resource("memory://hello", [] {
        return mcp::protocol::ResourceContents{
            .uri = "memory://hello",
            .mime_type = "text/plain",
            .text = std::string("hello from cxxmcp minimal server"),
        };
      })
      .raw_request([](const mcp::protocol::JsonRpcRequest& request)
                       -> std::optional<mcp::protocol::JsonRpcResponse> {
        if (request.method == "example/health") {
          return mcp::protocol::make_response(request.id, Json{{"ok", true}});
        }
        return std::nullopt;
      })
      .run();
}
