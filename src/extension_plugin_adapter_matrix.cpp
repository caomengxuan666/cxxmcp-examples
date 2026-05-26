#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "cxxmcp/adapters/adapter.hpp"
#include "cxxmcp/plugin/tool.hpp"
#include "cxxmcp/server.hpp"

namespace {
using Json = mcp::protocol::Json;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

class EchoPlugin final : public mcp::plugin::ToolPlugin {
public:
  std::vector<mcp::protocol::ToolDefinition> tools() const override {
    return {mcp::protocol::ToolDefinition{
        .title = "Plugin echo",
        .name = "plugin.echo",
        .description = "Echoes arguments through the plugin SDK contract.",
        .input_schema = Json{{"type", "object"}},
    }};
  }

  mcp::core::Result<mcp::protocol::ToolResult>
  call(const mcp::plugin::ToolExecutionContext &context) override {
    require(context.tool_name == "plugin.echo", "plugin tool name mismatch");
    mcp::protocol::ToolResult result;
    result.structured_content =
        Json{{"plugin", true}, {"arguments", context.arguments}};
    result.content.push_back(
        mcp::protocol::ContentBlock::text_content("plugin echo complete"));
    return result;
  }
};

class PluginAdapter final : public mcp::adapters::Adapter {
public:
  explicit PluginAdapter(mcp::plugin::ToolPlugin &plugin) : plugin_(plugin) {}

  std::string_view name() const noexcept override { return "plugin-adapter"; }

  mcp::core::Result<mcp::core::Unit>
  register_tools(mcp::server::ToolRegistry &registry) override {
    for (auto definition : plugin_.tools()) {
      const auto name = definition.name;
      auto added =
          registry.add(std::move(definition),
                       [this, name](const mcp::server::ToolContext &context) {
                         return plugin_.call(mcp::plugin::ToolExecutionContext{
                             .tool_name = name,
                             .arguments = context.arguments,
                         });
                       });
      if (!added) {
        return std::unexpected(added.error());
      }
    }
    return mcp::core::Unit{};
  }

private:
  mcp::plugin::ToolPlugin &plugin_;
};

} // namespace

int main() {
  try {
    auto built = mcp::server::ServerBuilder()
                     .name("cxxmcp-extension-matrix")
                     .version("0.1.0")
                     .build();
    require(built.has_value(), "server build failed");

    EchoPlugin plugin;
    PluginAdapter adapter(plugin);
    require(adapter.name() == "plugin-adapter", "adapter name mismatch");
    require(adapter.register_tools((*built)->tools()).has_value(),
            "adapter register_tools failed");

    const auto tools = (*built)->list_tools();
    require(tools.size() == 1 && tools.front().name == "plugin.echo",
            "registered plugin tool missing");

    const auto result = (*built)->call_tool("plugin.echo", Json{{"value", 42}},
                                            "extension-session");
    require(result.has_value(), "plugin tool call failed");
    require(result->structured_content.has_value() &&
                result->structured_content->at("plugin") == true,
            "plugin structured content mismatch");

    std::cout << "extension plugin adapter matrix passed\n";
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "extension plugin adapter matrix failed: " << ex.what()
              << '\n';
    return 1;
  }
}
