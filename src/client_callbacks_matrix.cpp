#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include "cxxmcp/cancellation.hpp"
#include "cxxmcp/client.hpp"
#include "cxxmcp/peer.hpp"

namespace {
using Json = mcp::protocol::Json;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

class NoopTransport final : public mcp::client::Transport {
 public:
  mcp::core::Result<mcp::protocol::JsonRpcResponse> send(
      const mcp::protocol::JsonRpcRequest& request) override {
    return mcp::protocol::make_response(request.id, Json::object());
  }

  mcp::core::Result<mcp::core::Unit> send_notification(
      const mcp::protocol::JsonRpcNotification&) override {
    return mcp::core::Unit{};
  }
};

const mcp::protocol::JsonRpcResponse& response_from(
    const mcp::core::Result<std::optional<mcp::protocol::JsonRpcMessage>>&
        dispatched,
    std::string_view label) {
  require(dispatched.has_value(), label);
  require(dispatched->has_value(), label);
  const auto* response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&**dispatched);
  require(response != nullptr, label);
  require(response->result.has_value(), label);
  return *response;
}

}  // namespace

int main() {
  try {
    mcp::ClientPeer peer(
        mcp::client::Client(std::make_unique<NoopTransport>()));

    int initialized = 0;
    int cancelled = 0;
    int logging = 0;
    int progress = 0;
    int list_changed = 0;
    int resource_updated = 0;
    int elicitation_complete = 0;
    int task_status = 0;
    int cancellation_aware_handlers = 0;

    peer.set_roots({mcp::protocol::Root{.uri = "file:///workspace",
                                        .name = "workspace"}})
        .on_initialized([&] { ++initialized; })
        .on_cancelled(
            [&](const mcp::protocol::RequestId&, std::string_view reason) {
              require(reason == "done", "cancel reason mismatch");
              ++cancelled;
            })
        .on_logging_message(
            [&](std::string_view level, std::string_view message) {
              require(level == "info", "logging level mismatch");
              require(message == "hello", "logging message mismatch");
              ++logging;
            })
        .on_progress(
            [&](const mcp::protocol::ProgressNotificationParams& params) {
              require(params.progress == 0.5, "progress value mismatch");
              ++progress;
            })
        .on_tool_list_changed([&] { ++list_changed; })
        .on_prompt_list_changed([&] { ++list_changed; })
        .on_resource_list_changed([&] { ++list_changed; })
        .on_roots_list_changed([&] { ++list_changed; })
        .on_resource_updated([&](const std::string& uri) {
          require(uri == "file:///workspace/data.txt",
                  "resource updated uri mismatch");
          ++resource_updated;
        })
        .on_elicitation_complete([&](std::string_view id) {
          require(id == "elicitation-1", "elicitation complete id mismatch");
          ++elicitation_complete;
        })
        .on_task_status([&](const mcp::protocol::Task& task) {
          require(task.task_id == "task-1", "task status id mismatch");
          require(task.status == mcp::protocol::TaskStatus::Completed,
                  "task status mismatch");
          ++task_status;
        })
        .on_list_roots_request(
            [&](mcp::CancellationToken cancellation)
                -> mcp::core::Result<mcp::protocol::RootsListResult> {
              require(!cancellation.cancelled(),
                      "roots cancellation should not be set");
              ++cancellation_aware_handlers;
              mcp::protocol::RootsListResult result;
              result.roots.push_back(mcp::protocol::Root{
                  .uri = "file:///workspace", .name = "workspace"});
              return result;
            })
        .on_create_message_request(
            [&](const mcp::protocol::CreateMessageParams& params,
                mcp::CancellationToken cancellation)
                -> mcp::core::Result<mcp::protocol::CreateMessageResult> {
              require(!cancellation.cancelled(),
                      "sampling cancellation should not be set");
              ++cancellation_aware_handlers;
              require(params.max_tokens == 64, "sampling max tokens mismatch");
              return mcp::protocol::CreateMessageResult{
                  .role = "assistant",
                  .content =
                      mcp::protocol::ContentBlock::text_content("sampled"),
                  .model = "client-callbacks-matrix",
              };
            })
        .on_create_elicitation_request(
            [&](const mcp::protocol::CreateElicitationRequestParam& params,
                mcp::CancellationToken cancellation)
                -> mcp::core::Result<mcp::protocol::CreateElicitationResult> {
              require(!cancellation.cancelled(),
                      "elicitation cancellation should not be set");
              ++cancellation_aware_handlers;
              require(params.message == "Choose owner",
                      "elicitation message mismatch");
              return mcp::protocol::CreateElicitationResult{
                  .action = mcp::protocol::ElicitationAction::Accept,
                  .content = Json{{"owner", "sdk"}},
              };
            })
        .on_custom_request([&](const mcp::protocol::JsonRpcRequest& request,
                               mcp::CancellationToken cancellation)
                               -> mcp::core::Result<Json> {
          require(!cancellation.cancelled(),
                  "custom cancellation should not be set");
          ++cancellation_aware_handlers;
          require(request.method == "client/custom", "custom method mismatch");
          return Json{{"custom", true}};
        });

    auto roots =
        response_from(peer.dispatch_message(mcp::protocol::JsonRpcRequest{
                          .method = std::string(mcp::protocol::RootsListMethod),
                          .params = Json::object(),
                          .id = std::int64_t{1},
                      }),
                      "roots/list dispatch failed");
    require(roots.result->at("roots").front().at("uri") == "file:///workspace",
            "roots/list result mismatch");

    mcp::protocol::CreateMessageParams sample_params;
    sample_params.messages.push_back(mcp::protocol::SamplingMessage{
        .role = "user",
        .content = mcp::protocol::ContentBlock::text_content("summarize"),
    });
    sample_params.max_tokens = 64;
    auto sample = response_from(
        peer.dispatch_message(mcp::protocol::JsonRpcRequest{
            .method = std::string(mcp::protocol::SamplingCreateMessageMethod),
            .params =
                mcp::protocol::create_message_params_to_json(sample_params),
            .id = std::int64_t{2},
        }),
        "sampling dispatch failed");
    require(sample.result->at("model") == "client-callbacks-matrix",
            "sampling result mismatch");

    auto schema = mcp::protocol::ElicitationSchema::Builder()
                      .required_string("owner")
                      .build();
    require(schema.has_value(), "elicitation schema build failed");
    auto elicitation = response_from(
        peer.dispatch_message(mcp::protocol::JsonRpcRequest{
            .method = std::string(mcp::protocol::ElicitationCreateMethod),
            .params = mcp::protocol::create_elicitation_request_param_to_json(
                mcp::protocol::CreateElicitationRequestParam{
                    .message = "Choose owner",
                    .requested_schema = *schema,
                }),
            .id = std::int64_t{3},
        }),
        "elicitation dispatch failed");
    require(elicitation.result->at("action") == "accept",
            "elicitation action mismatch");

    auto custom =
        response_from(peer.dispatch_message(mcp::protocol::JsonRpcRequest{
                          .method = "client/custom",
                          .params = Json::object(),
                          .id = std::int64_t{4},
                      }),
                      "custom request dispatch failed");
    require(custom.result->at("custom") == true, "custom request mismatch");

    require(peer
                .dispatch_message(mcp::protocol::JsonRpcNotification{
                    .method = std::string(mcp::protocol::InitializedMethod),
                    .params = Json::object(),
                })
                .has_value(),
            "initialized notification failed");
    require(peer
                .dispatch_message(mcp::protocol::JsonRpcNotification{
                    .method =
                        std::string(mcp::protocol::CancelledNotificationMethod),
                    .params = Json{{"requestId", 1}, {"reason", "done"}},
                })
                .has_value(),
            "cancelled notification failed");
    require(peer
                .dispatch_message(mcp::protocol::JsonRpcNotification{
                    .method = std::string(
                        mcp::protocol::LoggingMessageNotificationMethod),
                    .params = Json{{"level", "info"}, {"data", "hello"}},
                })
                .has_value(),
            "logging notification failed");
    require(peer
                .dispatch_message(mcp::protocol::JsonRpcNotification{
                    .method =
                        std::string(mcp::protocol::ProgressNotificationMethod),
                    .params = Json{{"progressToken", 7}, {"progress", 0.5}},
                })
                .has_value(),
            "progress notification failed");

    for (const auto method :
         {mcp::protocol::ToolsListChangedNotificationMethod,
          mcp::protocol::PromptsListChangedNotificationMethod,
          mcp::protocol::ResourcesListChangedNotificationMethod,
          mcp::protocol::RootsListChangedNotificationMethod}) {
      require(peer.dispatch_message(mcp::protocol::JsonRpcNotification{
                                        .method = std::string(method),
                                        .params = Json::object(),
                                    })
                  .has_value(),
              "list-changed notification failed");
    }
    require(peer
                .dispatch_message(mcp::protocol::JsonRpcNotification{
                    .method = std::string(
                        mcp::protocol::ResourcesUpdatedNotificationMethod),
                    .params = Json{{"uri", "file:///workspace/data.txt"}},
                })
                .has_value(),
            "resource-updated notification failed");
    require(peer
                .dispatch_message(mcp::protocol::JsonRpcNotification{
                    .method = std::string(
                        mcp::protocol::ElicitationCompleteNotificationMethod),
                    .params = Json{{"elicitationId", "elicitation-1"}},
                })
                .has_value(),
            "elicitation-complete notification failed");
    require(peer
                .dispatch_message(mcp::protocol::JsonRpcNotification{
                    .method = std::string(
                        mcp::protocol::TasksStatusNotificationMethod),
                    .params = mcp::protocol::task_to_json(mcp::protocol::Task{
                        .task_id = "task-1",
                        .status = mcp::protocol::TaskStatus::Completed,
                    }),
                })
                .has_value(),
            "task-status notification failed");

    require(initialized == 1, "initialized count mismatch");
    require(cancelled == 1, "cancelled count mismatch");
    require(logging == 1, "logging count mismatch");
    require(progress == 1, "progress count mismatch");
    require(list_changed == 4, "list changed count mismatch");
    require(resource_updated == 1, "resource updated count mismatch");
    require(elicitation_complete == 1, "elicitation complete count mismatch");
    require(task_status == 1, "task status count mismatch");
    require(cancellation_aware_handlers == 4,
            "cancellation-aware handler count mismatch");

    std::cout << "client callbacks matrix passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "client callbacks matrix failed: " << ex.what() << '\n';
    return 1;
  }
}
