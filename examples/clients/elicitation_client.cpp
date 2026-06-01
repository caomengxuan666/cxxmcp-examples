#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "cxxmcp/client.hpp"
#include "cxxmcp/protocol/elicitation.hpp"

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
    mcp::client::Client client(nullptr);
    client.on_create_elicitation_request(
        [](const mcp::protocol::CreateElicitationRequestParam &request)
            -> mcp::core::Result<mcp::protocol::CreateElicitationResult> {
          if (request.message == "decline") {
            return mcp::protocol::CreateElicitationResult{
                .action = mcp::protocol::ElicitationAction::Decline,
            };
          }
          return mcp::protocol::CreateElicitationResult{
              .action = mcp::protocol::ElicitationAction::Accept,
              .content = Json{{"project", "cxxmcp"}},
          };
        });

    const Json form_schema =
        Json{{"type", "object"},
             {"properties", Json{{"project", Json{{"type", "string"}}}}},
             {"required", Json::array({"project"})}};

    const auto accepted = client.handle_request(mcp::protocol::JsonRpcRequest{
        .method = std::string(mcp::protocol::ElicitationCreateMethod),
        .params = Json{{"message", "choose project"},
                       {"requestedSchema", form_schema}},
        .id = std::int64_t{1},
    });
    require(accepted.has_value(), "elicitation accept request failed");
    require(accepted->result.has_value(), "elicitation accept result missing");
    require(accepted->result->at("action") == "accept",
            "elicitation accept action mismatch");
    require(accepted->result->at("content").at("project") == "cxxmcp",
            "elicitation accept content mismatch");

    const auto declined = client.handle_request(mcp::protocol::JsonRpcRequest{
        .method = std::string(mcp::protocol::ElicitationCreateMethod),
        .params =
            Json{{"message", "decline"}, {"requestedSchema", form_schema}},
        .id = std::int64_t{2},
    });
    require(declined.has_value(), "elicitation decline request failed");
    require(declined->result.has_value(), "elicitation decline result missing");
    require(declined->result->at("action") == "decline",
            "elicitation decline action mismatch");

    std::cout << "elicitation client example passed\n";
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "elicitation client example failed: " << ex.what() << '\n';
    return 1;
  }
}
