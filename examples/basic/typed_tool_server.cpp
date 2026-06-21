#include <cstdint>
#include <string>

#include "cxxmcp/peer.hpp"
#include "cxxmcp/run.hpp"
#include "cxxmcp/server.hpp"

namespace examples {

struct AddTool {
  static constexpr std::string_view name = "math.add";
  static constexpr std::string_view title = "Add two integers";
  static constexpr std::string_view description =
      "Returns the sum and current MCP session id.";

  struct Args {
    std::int64_t left = 0;
    std::int64_t right = 0;

    CXXMCP_REFLECT_SELF(Args, left, right)
  };

  struct Result {
    std::int64_t sum = 0;
    std::string session_id;

    CXXMCP_REFLECT_SELF(Result, sum, session_id)
  };

  Result operator()(Args args, const mcp::server::ToolContext& context) const {
    return Result{
        .sum = args.left + args.right,
        .session_id = context.session_id,
    };
  }
};

}  // namespace examples

int main() {
  return mcp::ServerPeer::builder()
      .name("cxxmcp-typed-tool")
      .version("0.1.0")
      .instructions("Typed stdio server exposing a schema-backed math tool.")
      .stdio()
      .tool(mcp::server::tool(examples::AddTool{}))
      .run();
}
