#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "cxxmcp/client.hpp"
#include "cxxmcp/peer.hpp"
#include "cxxmcp/server.hpp"

namespace {
using Json = mcp::protocol::Json;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

class LoopbackTransport final : public mcp::client::Transport {
 public:
  explicit LoopbackTransport(mcp::server::Server& server) : server_(server) {}

  mcp::core::Result<mcp::protocol::JsonRpcResponse> send(
      const mcp::protocol::JsonRpcRequest& request) override {
    return server_.handle_request(request, context_);
  }

  mcp::core::Result<mcp::core::Unit> send_notification(
      const mcp::protocol::JsonRpcNotification& notification) override {
    notifications_.push_back(notification.method);
    return mcp::core::Unit{};
  }

  std::size_t notification_count() const noexcept {
    return notifications_.size();
  }

 private:
  mcp::server::Server& server_;
  mcp::server::SessionContext context_{
      .session_id = "examples-smoke",
      .remote_address = "loopback",
  };
  std::vector<std::string> notifications_;
};

bool wait_for_status(mcp::ClientPeer& peer, std::string_view task_id,
                     mcp::protocol::TaskStatus status) {
  for (int attempt = 0; attempt < 100; ++attempt) {
    auto task = peer.get_task(task_id);
    if (task.has_value() && task->status == status) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

}  // namespace

int main() {
  try {
    std::size_t logging_events = 0;

    auto built =
        mcp::ServerPeer::builder()
            .name("cxxmcp-examples-smoke")
            .version("0.1.0")
            .instructions("In-process downstream SDK validation server.")
            .task_manager(mcp::server::TaskOperationProcessorOptions{
                .worker_count = 1,
                .queue_size = 8,
                .poll_interval = std::int64_t{1},
            })
            .tool(mcp::server::tool<Json, Json>("analysis.echo")
                      .title("Echo analysis payload")
                      .description("Returns the request payload and session.")
                      .task_support(mcp::protocol::TaskSupport::Optional)
                      .handler([](const Json& args,
                                  const mcp::server::ToolContext& context) {
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(20));
                        return Json{{"input", args},
                                    {"session", context.session_id}};
                      }))
            .prompt(
                mcp::protocol::Prompt{
                    .name = "analysis.plan",
                    .description = "Plan an analysis workflow.",
                    .arguments = {mcp::protocol::PromptArgument{
                        .name = "goal",
                        .description = "Goal to analyze.",
                        .required = true,
                    }},
                },
                [](const mcp::server::PromptContext& context) {
                  mcp::protocol::PromptsGetResult result;
                  result.messages.push_back(mcp::protocol::PromptMessage{
                      .role = "user",
                      .content = mcp::protocol::ContentBlock::text_content(
                          "Plan analysis for " +
                          context.arguments.value("goal", std::string{})),
                  });
                  return result;
                })
            .resource("memory://status", [] {
              return mcp::protocol::ResourceContents{
                  .uri = "memory://status",
                  .mime_type = "application/json",
                  .text = R"({"ok":true,"source":"sdk_smoke"})",
              };
            })
            .resource_template("memory://artifact/{name}", [] {
              return mcp::protocol::ResourceTemplate{
                  .uri_template = "memory://artifact/{name}",
                  .name = "Memory artifact",
                  .description = "Advertised template for generated artifacts.",
                  .mime_type = "application/json",
              };
            })
            .completion([](const Json& request) {
              const auto prefix = request.value("prefix", std::string{});
              return Json{{"completion",
                           Json{{"values", Json::array({prefix + "plan",
                                                        prefix + "report"})},
                                {"total", 2}}}};
            })
            .sampling([](const Json& request) {
              return Json{{"role", "assistant"},
                          {"content",
                           Json{{"type", "text"},
                                {"text", "sample: " +
                                             request.value("prompt",
                                                           std::string{})}}},
                          {"model", "cxxmcp-examples-smoke"}};
            })
            .logging([&logging_events](std::string_view level,
                                       std::string_view message) {
              require(!level.empty(), "logging level missing");
              require(!message.empty(), "logging message missing");
              ++logging_events;
            })
            .raw_request([](const mcp::protocol::JsonRpcRequest& request)
                             -> std::optional<mcp::protocol::JsonRpcResponse> {
              if (request.method == "example/health") {
                return mcp::protocol::make_response(
                    request.id, Json{{"ok", true}, {"server", "smoke"}});
              }
              return std::nullopt;
            })
            .build();
    require(built.has_value(), "server build failed");

    auto transport = std::make_unique<LoopbackTransport>(built->server());
    auto* transport_ptr = transport.get();
    mcp::ClientPeer peer(mcp::client::Client(std::move(transport)));

    require(peer.initialize("cxxmcp-examples", "0.1.0").has_value(),
            "initialize failed");

    auto tools = peer.list_tools();
    require(tools.has_value() && tools->size() == 1, "list_tools failed");

    auto direct = peer.call_tool("analysis.echo", Json{{"value", 42}});
    require(direct.has_value(), "direct tool call failed");
    require(direct->structured_content.has_value(), "structured result absent");
    require(direct->structured_content->at("session") == "examples-smoke",
            "session did not flow through tool context");

    auto task = peer.call_tool_task_async(
        mcp::protocol::ToolCall{
            .name = "analysis.echo",
            .arguments = Json{{"value", "task"}},
            .task = mcp::protocol::TaskRequestParameters{
                .ttl = std::int64_t{60},
            },
        });
    auto created = task.await_response();
    require(created.has_value(), "task creation failed");
    require(wait_for_status(peer, created->task.task_id,
                            mcp::protocol::TaskStatus::Completed),
            "task did not complete");
    auto task_payload = peer.task_result(created->task.task_id);
    require(task_payload.has_value(), "task result failed");
    require(task_payload->at("structuredContent").at("input").at("value") ==
                "task",
            "task payload mismatch");

    auto prompts = peer.list_prompts();
    require(prompts.has_value() && prompts->size() == 1, "list_prompts failed");
    auto prompt = peer.get_prompt("analysis.plan", Json{{"goal", "release"}});
    require(prompt.has_value() && !prompt->messages.empty(),
            "get_prompt failed");

    auto resources = peer.list_resources();
    require(resources.has_value() && resources->size() == 1,
            "list_resources failed");
    auto resource = peer.read_resource("memory://status");
    require(resource.has_value() && !resource->contents.empty(),
            "read_resource failed");

    auto templates = peer.list_resource_templates();
    require(templates.has_value() && templates->size() == 1,
            "list_resource_templates failed");

    auto completion = peer.complete(Json{{"prefix", "triage."}});
    require(completion.has_value(), "completion failed");
    require(completion->at("completion").at("values").size() == 2,
            "completion payload mismatch");

    auto sample = peer.create_message(Json{{"prompt", "write a report"}});
    require(sample.has_value(), "sampling create_message failed");
    require(sample->at("role") == "assistant", "sampling role mismatch");

    require(peer.set_level("debug").has_value(), "logging set_level failed");
    require(logging_events == 1, "logging handler count mismatch");

    auto health = peer.raw_request(mcp::protocol::JsonRpcRequest{
        .method = "example/health",
        .params = Json::object(),
        .id = std::int64_t{99},
    });
    require(health.has_value() && health->value("ok", false),
            "raw health request failed");

    require(peer.notify_initialized().has_value(), "notify initialized failed");
    require(peer.notify_progress(std::int64_t{7}, 0.5, 1.0).has_value(),
            "notify progress failed");
    require(transport_ptr->notification_count() == 2,
            "client notifications not sent");

    std::cout << "cxxmcp examples SDK smoke passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "cxxmcp examples SDK smoke failed: " << ex.what() << '\n';
    return 1;
  }
}
