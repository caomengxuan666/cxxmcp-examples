#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

#include "cxxmcp/client.hpp"
#include "cxxmcp/client/handler.hpp"
#include "cxxmcp/server.hpp"
#include "cxxmcp/server/handler.hpp"

namespace {
using Json = mcp::protocol::Json;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

class NoopClientTransport final : public mcp::client::Transport {
public:
  mcp::core::Result<mcp::protocol::JsonRpcResponse>
  send(const mcp::protocol::JsonRpcRequest &request) override {
    return mcp::protocol::make_response(request.id, Json::object());
  }
};

struct ClientContract final : mcp::client::ClientHandlerInterface {
  mutable int initialized = 0;
  mutable int roots = 0;
  mutable int raw = 0;

  void on_initialized() const override { ++initialized; }

  std::optional<mcp::core::Result<mcp::protocol::RootsListResult>>
  on_list_roots_request() const override {
    ++roots;
    mcp::protocol::RootsListResult result;
    result.roots.push_back(
        mcp::protocol::Root{.uri = "file:///contract", .name = "contract"});
    return result;
  }

  std::optional<mcp::core::Result<Json>> on_custom_request(
      const mcp::protocol::JsonRpcRequest &request) const override {
    ++raw;
    return Json{{"method", request.method}, {"handled", true}};
  }
};

} // namespace

int main() {
  try {
    mcp::client::Client client(std::make_unique<NoopClientTransport>());
    ClientContract client_contract;
    client.set_handler(client_contract);

    require(client
                .handle_notification(mcp::protocol::JsonRpcNotification{
                    .method = std::string(mcp::protocol::InitializedMethod),
                    .params = Json::object(),
                })
                .has_value(),
            "client initialized notification failed");
    auto roots = client.handle_request(mcp::protocol::JsonRpcRequest{
        .method = std::string(mcp::protocol::RootsListMethod),
        .params = Json::object(),
        .id = std::int64_t{1},
    });
    require(roots.has_value() && roots->result.has_value() &&
                roots->result->at("roots").size() == 1,
            "client contract roots failed");
    auto custom = client.handle_request(mcp::protocol::JsonRpcRequest{
        .method = "client/contract",
        .params = Json::object(),
        .id = std::int64_t{2},
    });
    require(custom.has_value() && custom->result->at("handled") == true,
            "client contract custom failed");
    require(client_contract.initialized == 1 && client_contract.roots == 1 &&
                client_contract.raw == 1,
            "client contract counts mismatch");

    int call_tool_count = 0;
    int get_prompt_count = 0;
    int read_resource_count = 0;

    auto server = mcp::server::ServerBuilder()
                      .name("handler-interface")
                      .version("0.1.0")
                      .add_tool(
                          mcp::protocol::tool_definition("contract.echo")
                              .input<Json>()
                              .build(),
                          [&](const mcp::server::ToolContext &context)
                              -> mcp::core::Result<mcp::protocol::ToolResult> {
                            ++call_tool_count;
                            mcp::protocol::ToolResult result;
                            result.structured_content =
                                Json{{"echo", context.arguments},
                                     {"session", context.session_id}};
                            result.content.push_back(
                                mcp::protocol::ContentBlock::text_content(
                                    "contract echo"));
                            return result;
                          })
                      .add_prompt(
                          mcp::protocol::Prompt{.name = "contract.prompt"},
                          [&](const mcp::server::PromptContext &context)
                              -> mcp::core::Result<
                                  mcp::protocol::PromptsGetResult> {
                            ++get_prompt_count;
                            mcp::protocol::PromptsGetResult result;
                            result.messages.push_back(
                                mcp::protocol::PromptMessage{
                                    .role = "user",
                                    .content =
                                        mcp::protocol::ContentBlock::
                                            text_content(
                                                "render " +
                                                context.arguments.value(
                                                    "topic", std::string{})),
                                });
                            return result;
                          })
                      .add_resource(
                          mcp::protocol::Resource{
                              .uri = "contract://resource",
                              .name = "contract resource"},
                          [&](const mcp::server::ResourceContext &context)
                              -> mcp::core::Result<
                                  mcp::protocol::ResourcesReadResult> {
                            ++read_resource_count;
                            mcp::protocol::ResourcesReadResult result;
                            result.contents.push_back(
                                mcp::protocol::ResourceContents{
                                    .uri = context.uri,
                                    .mime_type = "text/plain",
                                    .text = std::string("contract resource"),
                                });
                            return result;
                          })
                      .build();
    require(server.has_value(), "server build failed");
    mcp::server::SessionContext server_context{.session_id = "handler-session",
                                               .remote_address = "loopback"};

    auto tools = (*server)->handle_request(
        mcp::protocol::JsonRpcRequest{
            .method = std::string(mcp::protocol::ToolsListMethod),
            .params = Json::object(),
            .id = std::int64_t{30},
        },
        server_context);
    require(tools.has_value() && tools->result.has_value() &&
                tools->result->at("tools").size() == 1,
            "server contract tools/list failed");
    auto tool_call = (*server)->handle_request(
        mcp::protocol::JsonRpcRequest{
            .method = std::string(mcp::protocol::ToolsCallMethod),
            .params = mcp::protocol::tool_call_to_json(mcp::protocol::ToolCall{
                .name = "contract.echo",
                .arguments = Json{{"value", 42}},
            }),
            .id = std::int64_t{31},
        },
        server_context);
    require(tool_call.has_value() && tool_call->result.has_value() &&
                tool_call->result->at("structuredContent").at("session") ==
                    "handler-session",
            "server contract tools/call failed");
    auto prompt = (*server)->handle_request(
        mcp::protocol::JsonRpcRequest{
            .method = std::string(mcp::protocol::PromptsGetMethod),
            .params = mcp::protocol::prompts_get_params_to_json(
                mcp::protocol::PromptsGetParams{
                    .name = "contract.prompt",
                    .arguments = Json{{"topic", "sdk"}},
                }),
            .id = std::int64_t{32},
        },
        server_context);
    require(prompt.has_value() && prompt->result.has_value() &&
                prompt->result->at("messages").size() == 1,
            "server contract prompts/get failed");
    auto resource = (*server)->handle_request(
        mcp::protocol::JsonRpcRequest{
            .method = std::string(mcp::protocol::ResourcesReadMethod),
            .params = mcp::protocol::resources_read_params_to_json(
                mcp::protocol::ResourcesReadParams{
                    .uri = "contract://resource",
                }),
            .id = std::int64_t{33},
        },
        server_context);
    require(resource.has_value() && resource->result.has_value() &&
                resource->result->at("contents").size() == 1,
            "server contract resources/read failed");

    require(call_tool_count == 1 && get_prompt_count == 1 &&
                read_resource_count == 1,
            "server contract execution counts mismatch");

    std::cout << "handler interface matrix passed\n";
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "handler interface matrix failed: " << ex.what() << '\n';
    return 1;
  }
}
