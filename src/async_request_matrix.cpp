#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "cxxmcp/cancellation.hpp"
#include "cxxmcp/client.hpp"
#include "cxxmcp/peer.hpp"
#include "cxxmcp/request.hpp"
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
  explicit LoopbackTransport(mcp::server::Server &server) : server_(server) {}

  mcp::core::Result<mcp::protocol::JsonRpcResponse>
  send(const mcp::protocol::JsonRpcRequest &request) override {
    if (request.method == "example/slow") {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return server_.handle_request(request, context_);
  }

  mcp::core::Result<mcp::core::Unit> send_notification(
      const mcp::protocol::JsonRpcNotification &notification) override {
    std::lock_guard lock(mutex_);
    notifications_.push_back(notification.method);
    return mcp::core::Unit{};
  }

  std::size_t notification_count(std::string_view method) const {
    std::lock_guard lock(mutex_);
    std::size_t count = 0;
    for (const auto &item : notifications_) {
      if (item == method) {
        ++count;
      }
    }
    return count;
  }

private:
  mcp::server::Server &server_;
  mcp::server::SessionContext context_{.session_id = "async-matrix",
                                       .remote_address = "loopback"};
  mutable std::mutex mutex_;
  std::vector<std::string> notifications_;
};

} // namespace

int main() {
  try {
    const auto configured = mcp::configure_request_executor(
        mcp::RequestExecutorOptions{.worker_count = 2, .max_queue_size = 16});
    require(configured.has_value(), "request executor configure failed");

    auto built =
        mcp::server::App::builder()
            .name("cxxmcp-async-matrix")
            .version("0.1.0")
            .tool(mcp::server::tool<Json, Json>("echo").handler(
                [](const Json &args) { return args; }))
            .prompt(
                mcp::protocol::Prompt{
                    .name = "triage",
                    .arguments = {mcp::protocol::PromptArgument{
                        .name = "topic",
                        .required = true,
                    }}},
                [](const mcp::server::PromptContext &context) {
                  mcp::protocol::PromptsGetResult result;
                  result.messages.push_back(mcp::protocol::PromptMessage{
                      .role = "user",
                      .content = mcp::protocol::ContentBlock::text_content(
                          context.arguments.value("topic", "")),
                  });
                  return result;
                })
            .resource("memory://async",
                      [] {
                        return mcp::protocol::ResourceContents{
                            .uri = "memory://async",
                            .text = "async resource",
                        };
                      })
            .resource_template("memory://artifact/{name}",
                               [] {
                                 return mcp::protocol::ResourceTemplate{
                                     .uri_template = "memory://artifact/{name}",
                                     .name = "artifact",
                                 };
                               })
            .completion([](const Json &request) {
              std::string prefix;
              if (request.contains("argument") &&
                  request.at("argument").contains("value")) {
                prefix = request.at("argument").at("value").get<std::string>();
              } else {
                prefix = request.value("prefix", std::string{});
              }
              return Json{{"completion",
                           Json{{"values", Json::array({prefix + "alpha",
                                                        prefix + "beta"})},
                                {"total", 2}}}};
            })
            .sampling([](const Json &) {
              return Json{
                  {"role", "assistant"},
                  {"content", Json{{"type", "text"}, {"text", "sampled"}}},
                  {"model", "async-matrix"}};
            })
            .raw_request([](const mcp::protocol::JsonRpcRequest &request)
                             -> std::optional<mcp::protocol::JsonRpcResponse> {
              if (request.method == "example/meta") {
                return mcp::protocol::make_response(
                    request.id,
                    Json{{"meta", request.meta.value_or(Json::object())}});
              }
              if (request.method == "example/slow") {
                return mcp::protocol::make_response(request.id,
                                                    Json{{"slow", true}});
              }
              if (request.method == mcp::protocol::ElicitationCreateMethod) {
                return mcp::protocol::make_response(
                    request.id, Json{{"action", "accept"},
                                     {"content", Json{{"ok", true}}}});
              }
              return std::nullopt;
            })
            .build();
    require(built.has_value(), "server build failed");

    auto transport = std::make_unique<LoopbackTransport>(**built);
    auto *transport_ptr = transport.get();
    mcp::ClientPeer peer(mcp::client::Client(std::move(transport)));

    require(peer.initialize("async-matrix", "0.1.0").has_value(),
            "initialize failed");

    mcp::RequestOptions options;
    options.timeout = std::chrono::milliseconds(500);
    options.meta = Json{{"traceId", "request-options"}};
    auto meta = peer.request_async("example/meta", Json::object(), options)
                    .await_response();
    require(meta.has_value(), "meta async request failed");
    require(meta->at("meta").at("traceId") == "request-options",
            "request meta did not flow");

    auto tools = peer.list_tools_async(options).await_response();
    require(tools.has_value() && tools->size() == 1, "list_tools_async failed");

    auto all_tools = peer.list_all_tools();
    require(all_tools.has_value() && all_tools->size() == 1,
            "list_all_tools failed");

    auto tool_get = peer.raw_request(mcp::protocol::JsonRpcRequest{
        .method = std::string(mcp::protocol::ToolsGetMethod),
        .params = Json{{"name", "echo"}},
        .id = std::int64_t{41},
    });
    require(tool_get.has_value() && tool_get->at("name") == "echo",
            "tools/get raw request failed");

    auto prompt =
        peer.get_prompt_async("triage", Json{{"topic", "release"}}, options)
            .await_response();
    require(prompt.has_value() && !prompt->messages.empty(),
            "get_prompt_async failed");

    auto all_prompts = peer.list_all_prompts();
    require(all_prompts.has_value() && all_prompts->size() == 1,
            "list_all_prompts failed");

    auto resource =
        peer.read_resource_async("memory://async", options).await_response();
    require(resource.has_value() && !resource->contents.empty(),
            "read_resource_async failed");

    auto all_resources = peer.list_all_resources();
    require(all_resources.has_value() && all_resources->size() == 1,
            "list_all_resources failed");

    auto all_templates = peer.list_all_resource_templates();
    require(all_templates.has_value() && all_templates->size() == 1,
            "list_all_resource_templates failed");

    auto prompt_completion =
        peer.complete_prompt_simple("triage", "topic", "re", Json::object());
    require(prompt_completion.has_value() && prompt_completion->size() == 2,
            "prompt completion helper failed");

    auto resource_completion = peer.complete_resource_simple(
        "memory://artifact/{name}", "name", "ar", Json::object());
    require(resource_completion.has_value() && resource_completion->size() == 2,
            "resource completion helper failed");

    auto sampled = peer.create_message_async(Json{{"prompt", "hello"}}, options)
                       .await_response();
    require(sampled.has_value() && sampled->at("model") == "async-matrix",
            "create_message_async failed");

    auto elicitation_schema = mcp::protocol::ElicitationSchema::Builder()
                                  .optional_string("note")
                                  .build();
    require(elicitation_schema.has_value(), "elicitation schema build failed");
    auto elicitation =
        peer.create_elicitation(mcp::protocol::CreateElicitationRequestParam{
            .message = "Need approval",
            .requested_schema = *elicitation_schema,
        });
    require(elicitation.has_value() &&
                elicitation->action == mcp::protocol::ElicitationAction::Accept,
            "create_elicitation failed");
    auto elicitation_async =
        peer.create_elicitation_async(
                Json{{"message", "Need approval async"},
                     {"requestedSchema",
                      mcp::protocol::elicitation_schema_to_json(
                          *elicitation_schema)}},
                options)
            .await_response();
    require(elicitation_async.has_value() &&
                elicitation_async->at("action") == "accept",
            "create_elicitation_async failed");

    mcp::RequestOptions timeout_options;
    timeout_options.timeout = std::chrono::milliseconds(10);
    auto timed_out =
        peer.request_async("example/slow", Json::object(), timeout_options)
            .await_response();
    require(!timed_out.has_value(), "timeout request unexpectedly succeeded");
    require(transport_ptr->notification_count(
                mcp::protocol::CancelledNotificationMethod) >= 1,
            "timeout did not send cancelled notification");

    mcp::CancellationSource source;
    mcp::RequestOptions cancelled_options;
    cancelled_options.cancellation_token = source.token();
    auto handle =
        peer.request_async("example/slow", Json::object(), cancelled_options);
    source.cancel();
    auto cancelled = handle.await_response();
    require(!cancelled.has_value(), "cancelled request unexpectedly succeeded");
    require(handle.cancel("manual follow-up").has_value(),
            "manual request cancel failed");

    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    std::cout << "async request matrix passed\n";
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "async request matrix failed: " << ex.what() << '\n';
    return 1;
  }
}
