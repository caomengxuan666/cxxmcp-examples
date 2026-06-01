#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "cxxmcp/peer.hpp"
#include "cxxmcp/server.hpp"

namespace {
using Json = mcp::protocol::Json;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

class InteractiveClientTransport final : public mcp::server::Transport {
 public:
  mcp::core::Result<mcp::core::Unit> start(
      mcp::server::RequestHandler,
      mcp::server::NotificationHandler = {}) override {
    return mcp::core::Unit{};
  }

  mcp::core::Result<mcp::protocol::JsonRpcResponse> send_request(
      const mcp::protocol::JsonRpcRequest& request) override {
    requests.push_back(request.method);
    if (request.method == mcp::protocol::RootsListMethod) {
      mcp::protocol::RootsListResult result;
      result.roots.push_back(
          mcp::protocol::Root{.uri = "file:///repo", .name = "repo"});
      return mcp::protocol::make_response(
          request.id, mcp::protocol::roots_list_result_to_json(result));
    }
    if (request.method == mcp::protocol::SamplingCreateMessageMethod) {
      return mcp::protocol::make_response(
          request.id,
          mcp::protocol::create_message_result_to_json(
              mcp::protocol::CreateMessageResult{
                  .role = "assistant",
                  .content = mcp::protocol::ContentBlock::text_content(
                      "sample from client"),
                  .model = "interactive-client",
              }));
    }
    if (request.method == mcp::protocol::ElicitationCreateMethod) {
      return mcp::protocol::make_response(
          request.id,
          mcp::protocol::create_elicitation_result_to_json(
              mcp::protocol::CreateElicitationResult{
                  .action = mcp::protocol::ElicitationAction::Accept,
                  .content = Json{{"approved", true}},
              }));
    }
    if (request.method == mcp::protocol::TasksListMethod) {
      mcp::protocol::TaskListResult result;
      result.tasks.push_back(mcp::protocol::Task{
          .task_id = "client-task-1",
          .status = mcp::protocol::TaskStatus::Completed,
      });
      return mcp::protocol::make_response(
          request.id, mcp::protocol::task_list_result_to_json(result));
    }
    if (request.method == mcp::protocol::TasksGetMethod) {
      return mcp::protocol::make_response(
          request.id, mcp::protocol::task_to_json(mcp::protocol::Task{
                          .task_id = "client-task-1",
                          .status = mcp::protocol::TaskStatus::Completed,
                      }));
    }
    if (request.method == mcp::protocol::TasksCancelMethod) {
      return mcp::protocol::make_response(
          request.id, mcp::protocol::task_to_json(mcp::protocol::Task{
                          .task_id = "client-task-1",
                          .status = mcp::protocol::TaskStatus::Cancelled,
                      }));
    }
    if (request.method == mcp::protocol::TasksResultMethod) {
      return mcp::protocol::make_response(request.id,
                                          Json{{"clientTaskResult", true}});
    }
    return mcp::protocol::make_error_response(
        request.id,
        mcp::protocol::make_error(
            static_cast<int>(mcp::protocol::ErrorCode::MethodNotFound),
            "not scripted"));
  }

  mcp::core::Result<mcp::protocol::JsonRpcResponse> send_request_to_session(
      std::string_view session_id,
      const mcp::protocol::JsonRpcRequest& request) override {
    require(session_id == "interactive-session", "session route mismatch");
    return send_request(request);
  }

  std::optional<mcp::protocol::ClientCapabilities>
  client_capabilities_for_session(std::string_view session_id) const override {
    require(session_id == "interactive-session", "capability session mismatch");
    mcp::protocol::ClientCapabilities capabilities;
    capabilities.roots.enabled = true;
    capabilities.roots.list_changed = true;
    capabilities.sampling.enabled = true;
    capabilities.sampling.tools = true;
    capabilities.elicitation.form = true;
    capabilities.elicitation.url = true;
    capabilities.tasks = mcp::protocol::TaskCapabilities{
        .list = true,
        .cancel = true,
    };
    return capabilities;
  }

  mcp::core::Result<mcp::core::Unit> send_notification(
      const mcp::protocol::JsonRpcNotification& notification) override {
    notifications.push_back(notification.method);
    return mcp::core::Unit{};
  }

  mcp::core::Result<mcp::core::Unit> send_notification_to_session(
      std::string_view session_id,
      const mcp::protocol::JsonRpcNotification& notification) override {
    require(session_id == "interactive-session",
            "notification session mismatch");
    return send_notification(notification);
  }

  void stop() noexcept override {}
  std::string_view name() const noexcept override {
    return "interactive-client";
  }

  std::vector<std::string> requests;
  std::vector<std::string> notifications;
};

}  // namespace

int main() {
  try {
    auto built =
        mcp::ServerPeer::builder()
            .name("cxxmcp-server-to-client-context")
            .version("0.1.0")
            .tool(
                mcp::server::tool<Json, Json>("client.roundtrip")
                    .handler([](const mcp::server::ToolContext& context) {
                      const auto client = context.client();
                      require(client.available(), "client peer unavailable");
                      require(client.supports_roots(), "roots not advertised");
                      require(client.supports_sampling_tools(),
                              "sampling not advertised");
                      require(client.supports_elicitation(),
                              "elicitation not advertised");

                      const auto roots = client.list_roots();
                      require(roots.has_value() && roots->roots.size() == 1,
                              "client roots/list failed");

                      mcp::protocol::CreateMessageParams sample_params;
                      sample_params.messages.push_back(
                          mcp::protocol::SamplingMessage{
                              .role = "user",
                              .content =
                                  mcp::protocol::ContentBlock::text_content(
                                      "summarize context"),
                          });
                      sample_params.max_tokens = 32;
                      const auto sample = client.create_message(sample_params);
                      require(sample.has_value() &&
                                  sample->model == "interactive-client",
                              "client sampling failed");

                      auto schema = mcp::protocol::ElicitationSchema::Builder()
                                        .required_bool("approved")
                                        .build();
                      require(schema.has_value(), "schema build failed");
                      const auto elicitation = client.create_elicitation(
                          mcp::protocol::CreateElicitationRequestParam{
                              .message = "Approve server action",
                              .requested_schema = *schema,
                          });
                      require(elicitation.has_value() &&
                                  elicitation->action ==
                                      mcp::protocol::ElicitationAction::Accept,
                              "client elicitation failed");

                      const auto client_tasks = client.list_tasks();
                      require(
                          client_tasks.has_value() && client_tasks->size() == 1,
                          "client task list failed");
                      require(client
                                  .notify_elicitation_complete(
                                      "elicitation-context-1")
                                  .has_value(),
                              "notify elicitation complete failed");

                      return Json{{"root", roots->roots.front().uri},
                                  {"sampleModel", sample->model},
                                  {"approved", elicitation->content->value(
                                                   "approved", false)},
                                  {"clientTasks", client_tasks->size()}};
                    }))
            .build();
    require(built.has_value(), "server build failed");

    InteractiveClientTransport transport;
    mcp::server::SessionContext context{.session_id = "interactive-session",
                                        .remote_address = "loopback",
                                        .transport = &transport};
    auto response = built->handle_request(
        mcp::protocol::JsonRpcRequest{
            .method = std::string(mcp::protocol::ToolsCallMethod),
            .params = mcp::protocol::tool_call_to_json(mcp::protocol::ToolCall{
                .name = "client.roundtrip",
                .arguments = Json::object(),
            }),
            .id = std::int64_t{1},
        },
        context);
    if (!response.has_value()) {
      throw std::runtime_error("tools/call dispatch failed: " +
                               response.error().message);
    }
    if (response->error.has_value()) {
      std::string detail = response->error->message;
      if (response->error->data.has_value()) {
        detail += ": " + response->error->data->dump();
      }
      throw std::runtime_error("tools/call failed: " + detail);
    }
    require(response->result.has_value(), "tools/call result missing");
    require(response->result->at("structuredContent").at("approved") == true,
            "server-to-client structured result mismatch");
    require(!transport.requests.empty(), "client requests missing");
    require(transport.notifications.size() == 1,
            "client notification count mismatch");

    std::cout << "server-to-client context matrix passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "server-to-client context matrix failed: " << ex.what()
              << '\n';
    return 1;
  }
}
