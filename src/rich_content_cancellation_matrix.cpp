#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "cxxmcp/cancellation.hpp"
#include "cxxmcp/peer.hpp"
#include "cxxmcp/server.hpp"

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
    auto built =
        mcp::ServerPeer::builder()
            .name("rich-content-cancellation")
            .version("0.1.0")
            .tool(mcp::server::tool<Json, Json>("content.rich")
                      .output_schema(
                          Json{{"type", "object"},
                               {"properties",
                                Json{{"ok", Json{{"type", "boolean"}}}}}})
                      .handler([](const Json &) {
                        mcp::protocol::ToolResult result;
                        result.structured_content = Json{{"ok", true}};
                        result.content.push_back(
                            mcp::protocol::ContentBlock::text_content("text"));
                        result.content.push_back(
                            mcp::protocol::ContentBlock::image("aW1hZ2U=",
                                                               "image/png"));
                        result.content.push_back(
                            mcp::protocol::ContentBlock::audio("YXVkaW8=",
                                                               "audio/wav"));
                        result.content.push_back(
                            mcp::protocol::ContentBlock::embedded_resource(
                                mcp::protocol::ResourceContents{
                                    .uri = "memory://embedded",
                                    .mime_type = "text/plain",
                                    .text = "embedded resource",
                                }));
                        result.content.push_back(
                            mcp::protocol::ContentBlock::resource_link_content(
                                mcp::protocol::Resource{
                                    .uri = "memory://linked",
                                    .name = "linked resource",
                                    .mime_type = "text/plain",
                                }));
                        result.content.front().annotations =
                            Json{{"audience", "user"}};
                        result.content.front().meta = Json{{"trace", "rich"}};
                        return result;
                      }))
            .tool(mcp::server::tool<Json, Json>("cancel.observe")
                      .handler([](const Json &,
                                  const mcp::server::ToolContext &context) {
                        return Json{{"cancelled", context.cancelled()}};
                      }))
            .build();
    require(built.has_value(), "server build failed");

    const auto rich =
        built->call_tool("content.rich", Json::object(), "rich-session");
    require(rich.has_value(), "rich content tool failed");
    require(rich->content.size() == 5, "rich content block count mismatch");
    require(rich->content.at(1).is_image(), "image block missing");
    require(rich->content.at(2).is_audio(), "audio block missing");
    require(rich->content.at(3).is_embedded_resource(),
            "embedded resource block missing");
    require(rich->content.at(4).is_resource_link(),
            "resource link block missing");
    require(rich->content.front().meta.has_value(), "content meta missing");

    mcp::CancellationSource cancellation;
    cancellation.cancel();
    const auto cancelled = built->server().tools().call(
        mcp::protocol::ToolCall{.name = "cancel.observe",
                                .arguments = Json::object()},
        mcp::server::SessionContext{.session_id = "cancel-session"},
        cancellation.token());
    require(cancelled.has_value(), "cancel observe tool failed");
    require(cancelled->structured_content->at("cancelled") == true,
            "tool context cancellation was not observed");

    std::cout << "rich content cancellation matrix passed\n";
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "rich content cancellation matrix failed: " << ex.what()
              << '\n';
    return 1;
  }
}
