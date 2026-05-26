#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
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
  explicit LoopbackTransport(mcp::server::Server &server) : server_(server) {}

  mcp::core::Result<mcp::protocol::JsonRpcResponse>
  send(const mcp::protocol::JsonRpcRequest &request) override {
    return server_.handle_request(request, context_);
  }

  mcp::core::Result<mcp::core::Unit> send_notification(
      const mcp::protocol::JsonRpcNotification &notification) override {
    notifications.push_back(notification.method);
    return mcp::core::Unit{};
  }

  std::vector<std::string> notifications;

private:
  mcp::server::Server &server_;
  mcp::server::SessionContext context_{.session_id = "task-cancel",
                                       .remote_address = "loopback"};
};

bool wait_for_status(mcp::ClientPeer &peer, std::string_view task_id,
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

} // namespace

int main() {
  try {
    auto built =
        mcp::server::App::builder()
            .name("task-cancel")
            .version("0.1.0")
            .tasks(mcp::server::TaskOperationProcessorOptions{
                .worker_count = 1,
                .queue_size = 8,
                .poll_interval = std::int64_t{1},
            })
            .tool(mcp::server::tool<Json, Json>("slow.cancel")
                      .task_support(mcp::protocol::TaskSupport::Optional)
                      .handler([](const Json &,
                                  const mcp::server::ToolContext &context) {
                        for (int i = 0; i < 100; ++i) {
                          if (context.cancelled()) {
                            return Json{{"cancelled", true}};
                          }
                          std::this_thread::sleep_for(
                              std::chrono::milliseconds(10));
                        }
                        return Json{{"cancelled", false}};
                      }))
            .build();
    require(built.has_value(), "server build failed");

    mcp::ClientPeer peer(
        mcp::client::Client(std::make_unique<LoopbackTransport>(**built)));
    auto task = peer.call_tool_task(mcp::protocol::ToolCall{
        .name = "slow.cancel",
        .arguments = Json::object(),
        .task = mcp::protocol::TaskRequestParameters{.ttl = std::int64_t{60}},
    });
    require(task.has_value(), "task creation failed");
    auto cancelled = peer.cancel_task(task->task.task_id);
    require(cancelled.has_value(), "task cancel request failed");
    require(wait_for_status(peer, task->task.task_id,
                            mcp::protocol::TaskStatus::Cancelled),
            "task did not reach cancelled status");
    auto tasks = peer.list_tasks();
    require(tasks.has_value() && !tasks->empty(), "list tasks failed");
    std::cout << "task cancel matrix passed\n";
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "task cancel matrix failed: " << ex.what() << '\n';
    return 1;
  }
}
