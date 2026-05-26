#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include "cxxmcp/client.hpp"
#include "cxxmcp/peer.hpp"

namespace {
using Json = mcp::protocol::Json;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

class PagingTransport final : public mcp::client::Transport {
public:
  mcp::core::Result<mcp::protocol::JsonRpcResponse>
  send(const mcp::protocol::JsonRpcRequest &request) override {
    const auto cursor = request.params.value("cursor", std::string{});
    if (request.method == mcp::protocol::ToolsListMethod) {
      return mcp::protocol::make_response(
          request.id,
          page("tools", cursor,
               Json{{"name", cursor.empty() ? "tool.one" : "tool.two"},
                    {"inputSchema", Json{{"type", "object"}}}}));
    }
    if (request.method == mcp::protocol::PromptsListMethod) {
      return mcp::protocol::make_response(
          request.id,
          page("prompts", cursor,
               Json{{"name", cursor.empty() ? "prompt.one" : "prompt.two"}}));
    }
    if (request.method == mcp::protocol::ResourcesListMethod) {
      return mcp::protocol::make_response(
          request.id,
          page("resources", cursor,
               Json{{"uri", cursor.empty() ? "memory://one" : "memory://two"},
                    {"name", cursor.empty() ? "one" : "two"}}));
    }
    if (request.method == mcp::protocol::ResourcesTemplatesListMethod) {
      return mcp::protocol::make_response(
          request.id,
          page("resourceTemplates", cursor,
               Json{{"uriTemplate",
                     cursor.empty() ? "memory://{one}" : "memory://{two}"},
                    {"name", cursor.empty() ? "one" : "two"}}));
    }
    if (request.method == mcp::protocol::TasksListMethod) {
      return mcp::protocol::make_response(
          request.id,
          page("tasks", cursor,
               mcp::protocol::task_to_json(mcp::protocol::Task{
                   .task_id = cursor.empty() ? "task-one" : "task-two",
                   .status = mcp::protocol::TaskStatus::Completed,
               })));
    }
    return mcp::protocol::make_error_response(
        request.id,
        mcp::protocol::make_error(
            static_cast<int>(mcp::protocol::ErrorCode::MethodNotFound),
            "not scripted"));
  }

private:
  static Json page(std::string key, std::string_view cursor, Json item) {
    Json result{{std::move(key), Json::array({std::move(item)})}};
    if (cursor.empty()) {
      result["nextCursor"] = "page-2";
    }
    return result;
  }
};

} // namespace

int main() {
  try {
    mcp::ClientPeer peer(
        mcp::client::Client(std::make_unique<PagingTransport>()));
    require(peer.list_all_tools()->size() == 2, "paged tools mismatch");
    require(peer.list_all_prompts()->size() == 2, "paged prompts mismatch");
    require(peer.list_all_resources()->size() == 2, "paged resources mismatch");
    require(peer.list_all_resource_templates()->size() == 2,
            "paged resource templates mismatch");
    require(peer.list_all_tasks()->size() == 2, "paged tasks mismatch");
    std::cout << "pagination cursor matrix passed\n";
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "pagination cursor matrix failed: " << ex.what() << '\n';
    return 1;
  }
}
