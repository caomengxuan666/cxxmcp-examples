#include <cstdint>
#include <string>
#include <utility>

#include "cxxmcp/peer.hpp"
#include "cxxmcp/run.hpp"
#include "cxxmcp/server.hpp"

namespace examples {

using Json = mcp::protocol::Json;

struct AddArgs {
  std::int64_t left = 0;
  std::int64_t right = 0;
};

struct AddResult {
  std::int64_t sum = 0;
  std::string session_id;
};

void from_json(const Json &json, AddArgs &args) {
  args.left = json.at("left").get<std::int64_t>();
  args.right = json.at("right").get<std::int64_t>();
}

void to_json(Json &json, const AddResult &result) {
  json = Json{{"sum", result.sum}, {"sessionId", result.session_id}};
}

} // namespace examples

namespace mcp::protocol {

template <> struct SchemaTraits<examples::AddArgs> {
  static Json schema() {
    return object_schema()
        .required_property("left", JsonSchema::integer())
        .required_property("right", JsonSchema::integer())
        .additional_properties(false)
        .build();
  }
};

template <> struct SchemaTraits<examples::AddResult> {
  static Json schema() {
    return object_schema()
        .required_property("sum", JsonSchema::integer())
        .required_property("sessionId", JsonSchema::string())
        .additional_properties(false)
        .build();
  }
};

} // namespace mcp::protocol

int main() {
  return mcp::ServerPeer::builder()
      .name("cxxmcp-typed-tool")
      .version("0.1.0")
      .instructions("Typed stdio server exposing a schema-backed math tool.")
      .stdio()
      .tool(
          mcp::server::tool<examples::AddArgs, examples::AddResult>("math.add")
              .title("Add two integers")
              .description("Returns the sum and current MCP session id.")
              .handler([](examples::AddArgs args,
                          const mcp::server::ToolContext &context) {
                return examples::AddResult{
                    .sum = args.left + args.right,
                    .session_id = context.session_id,
                };
              }))
      .run();
}
