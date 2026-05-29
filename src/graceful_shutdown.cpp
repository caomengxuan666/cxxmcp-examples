#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "cxxmcp/peer.hpp"
#include "cxxmcp/server.hpp"
#include "cxxmcp/service.hpp"
#include "cxxmcp/transport.hpp"

namespace {

using Json = mcp::protocol::Json;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

class BlockingServerTransport final : public mcp::transport::ServerTransport {
public:
  std::string_view name() const noexcept override { return "blocking-example"; }

  mcp::protocol::Json diagnostics() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    return Json{{"name", "blocking-example"}, {"closed", closed_}};
  }

  mcp::core::Result<mcp::core::Unit> send(TxMessage message) override {
    std::lock_guard<std::mutex> lock(mutex_);
    sent_.push_back(std::move(message));
    return mcp::core::Unit{};
  }

  mcp::core::Result<std::optional<RxMessage>> receive() override {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [&] { return closed_ || !incoming_.empty(); });
    if (closed_) {
      return std::nullopt;
    }
    auto message = std::move(incoming_.front());
    incoming_.pop_front();
    return message;
  }

  mcp::core::Result<mcp::core::Unit> close() override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      closed_ = true;
    }
    cv_.notify_all();
    return mcp::core::Unit{};
  }

private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<RxMessage> incoming_;
  std::deque<TxMessage> sent_;
  bool closed_ = false;
};

} // namespace

int main() {
  try {
    auto server = mcp::ServerPeer::builder()
                      .name("graceful-shutdown")
                      .version("0.1.0")
                      .tool(mcp::server::tool<Json, Json>("echo").handler(
                          [](const Json &args) { return args; }))
                      .build();
    require(server.has_value(), "server build failed");

    auto running = mcp::serve(mcp::ServerPeer(std::move(*server)),
                              std::make_unique<BlockingServerTransport>());
    require(running.has_value(), "server service failed to start");
    require(running->running(), "server service is not running");
    require(!running->cancellation_token().cancelled(),
            "service token cancelled too early");

    require(running->stop().has_value(), "server service stop failed");
    require(running->cancellation_token().cancelled(),
            "service token was not cancelled");
    require(!running->running(), "server service still running after stop");
    require(running->wait().has_value(), "server service wait failed");

    std::cout << "graceful shutdown example passed\n";
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "graceful shutdown example failed: " << ex.what() << '\n';
    return 1;
  }
}
