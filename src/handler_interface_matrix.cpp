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

struct ServerContract final : mcp::server::ServerHandlerInterface {
  mutable int logging = 0;
  mutable int progress = 0;

  std::optional<mcp::core::Result<Json>>
  on_completion(const Json &request) const override {
    return Json{{"completion",
                 Json{{"values", Json::array({request.value("prefix", "") +
                                              std::string("contract")})}}}};
  }

  void on_logging(std::string_view level, std::string_view) const override {
    require(level == "info", "server logging level mismatch");
    ++logging;
  }

  std::optional<mcp::protocol::JsonRpcResponse>
  on_raw_request(const mcp::protocol::JsonRpcRequest &request,
                 const mcp::server::SessionContext &) const override {
    if (request.method == "contract/raw") {
      return mcp::protocol::make_response(request.id, Json{{"raw", true}});
    }
    return std::nullopt;
  }

  std::optional<mcp::core::Result<mcp::core::Unit>> on_progress(
      const mcp::protocol::ProgressNotificationParams &params) const override {
    require(params.progress == 0.25, "server progress mismatch");
    ++progress;
    return mcp::core::Unit{};
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

    auto server = mcp::server::ServerBuilder()
                      .name("handler-interface")
                      .version("0.1.0")
                      .build();
    require(server.has_value(), "server build failed");
    ServerContract server_contract;
    (*server)->set_handler(server_contract);
    mcp::server::SessionContext server_context{.session_id = "handler-session",
                                               .remote_address = "loopback"};

    auto completion = (*server)->handle_request(
        mcp::protocol::JsonRpcRequest{
            .method = std::string(mcp::protocol::CompletionCompleteMethod),
            .params = Json{{"prefix", "sdk."}},
            .id = std::int64_t{3},
        },
        server_context);
    require(completion.has_value() && completion->result.has_value(),
            "server contract completion failed");
    require(
        (*server)
            ->handle_request(
                mcp::protocol::JsonRpcRequest{
                    .method = std::string(mcp::protocol::LoggingSetLevelMethod),
                    .params = Json{{"level", "info"}},
                    .id = std::int64_t{4},
                },
                server_context)
            .has_value(),
        "server contract logging failed");
    auto raw = (*server)->handle_request(
        mcp::protocol::JsonRpcRequest{
            .method = "contract/raw",
            .params = Json::object(),
            .id = std::int64_t{5},
        },
        server_context);
    require(raw.has_value() && raw->result->at("raw") == true,
            "server contract raw failed");
    require(
        (*server)
            ->handle_notification(
                mcp::protocol::JsonRpcNotification{
                    .method =
                        std::string(mcp::protocol::ProgressNotificationMethod),
                    .params = Json{{"progressToken", 1}, {"progress", 0.25}},
                },
                server_context)
            .has_value(),
        "server contract progress failed");
    require(server_contract.logging == 1 && server_contract.progress == 1,
            "server contract counts mismatch");

    std::cout << "handler interface matrix passed\n";
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "handler interface matrix failed: " << ex.what() << '\n';
    return 1;
  }
}
