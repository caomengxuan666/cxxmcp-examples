#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include "cxxmcp/peer.hpp"
#include "cxxmcp/run.hpp"
#include "cxxmcp/server.hpp"

namespace examples {
namespace fs = std::filesystem;
using Json = mcp::protocol::Json;

struct SummaryArgs {
  std::string path;
  int max_bytes = 16 * 1024 * 1024;
  int sample = 10;
};

struct FindArgs {
  std::string path;
  std::string file_contains;
  int max_results = 50;
  int max_bytes = 16 * 1024 * 1024;
};

struct ReadResult {
  fs::path path;
  std::string text;
  bool truncated = false;
  std::string error;
};

void from_json(const Json &json, SummaryArgs &args) {
  args.path = json.at("path").get<std::string>();
  args.max_bytes = json.value("max_bytes", args.max_bytes);
  args.sample = json.value("sample", args.sample);
}

void from_json(const Json &json, FindArgs &args) {
  args.path = json.at("path").get<std::string>();
  args.file_contains = json.value("file_contains", std::string{});
  args.max_results = json.value("max_results", args.max_results);
  args.max_bytes = json.value("max_bytes", args.max_bytes);
}

std::string slash_path(const fs::path &path) {
  const auto value = path.generic_string();
  return value.empty() ? std::string{"."} : value;
}

fs::path resolve_path(const std::string &requested) {
  std::error_code ec;
  fs::path path = requested.empty() ? fs::path{"."} : fs::path{requested};
  if (path.is_relative()) {
    path = fs::absolute(path, ec);
  }
  const auto canonical = fs::weakly_canonical(path, ec);
  return ec ? path.lexically_normal() : canonical;
}

ReadResult read_bounded(const std::string &requested, int max_bytes) {
  ReadResult result;
  result.path = resolve_path(requested);
  std::error_code ec;
  if (!fs::exists(result.path, ec)) {
    result.error = "file does not exist";
    return result;
  }
  if (!fs::is_regular_file(result.path, ec)) {
    result.error = "path is not a regular file";
    return result;
  }
  std::ifstream input(result.path, std::ios::binary);
  if (!input) {
    result.error = "unable to open file";
    return result;
  }
  const auto limit = std::clamp(max_bytes, 1, 64 * 1024 * 1024);
  result.text.assign(static_cast<std::size_t>(limit), '\0');
  input.read(result.text.data(),
             static_cast<std::streamsize>(result.text.size()));
  result.text.resize(static_cast<std::size_t>(input.gcount()));
  result.truncated = input.peek() != std::ifstream::traits_type::eof();
  return result;
}

std::string value_string(const Json &object, const char *key) {
  const auto it = object.find(key);
  if (it == object.end() || !it->is_string()) {
    return {};
  }
  return it->get<std::string>();
}

mcp::protocol::ToolResult error_result(std::string tool, const fs::path &path,
                                       std::string message) {
  mcp::protocol::ToolResult result =
      mcp::protocol::ToolResult::error_text(message);
  result.structured_content = Json{{"tool", std::move(tool)},
                                   {"path", slash_path(path)},
                                   {"error", std::move(message)}};
  return result;
}

mcp::protocol::ToolResult compile_commands_summary(SummaryArgs args) {
  args.max_bytes = std::clamp(args.max_bytes, 1, 64 * 1024 * 1024);
  args.sample = std::clamp(args.sample, 0, 100);
  auto file = read_bounded(args.path, args.max_bytes);
  if (!file.error.empty()) {
    return error_result("compile_commands.summary", file.path,
                        std::move(file.error));
  }
  try {
    const auto parsed = Json::parse(file.text);
    if (!parsed.is_array()) {
      return error_result("compile_commands.summary", file.path,
                          "compile_commands.json root is not an array");
    }
    Json samples = Json::array();
    Json directories = Json::object();
    int sampled = 0;
    for (const auto &item : parsed) {
      if (!item.is_object()) {
        continue;
      }
      const auto directory = value_string(item, "directory");
      if (!directory.empty()) {
        directories[directory] = directories.value(directory, 0) + 1;
      }
      if (sampled < args.sample) {
        samples.push_back(Json{{"file", value_string(item, "file")},
                               {"directory", directory}});
        ++sampled;
      }
    }
    Json structured = Json{{"tool", "compile_commands.summary"},
                           {"path", slash_path(file.path)},
                           {"entries", parsed.size()},
                           {"directories", directories},
                           {"sample", samples},
                           {"inputTruncated", file.truncated}};
    mcp::protocol::ToolResult result;
    result.content.push_back(
        mcp::protocol::ContentBlock::text_content(structured.dump(2)));
    result.structured_content = std::move(structured);
    return result;
  } catch (const std::exception &ex) {
    return error_result("compile_commands.summary", file.path, ex.what());
  }
}

mcp::protocol::ToolResult compile_commands_find(FindArgs args) {
  args.max_results = std::clamp(args.max_results, 1, 1000);
  args.max_bytes = std::clamp(args.max_bytes, 1, 64 * 1024 * 1024);
  auto file = read_bounded(args.path, args.max_bytes);
  if (!file.error.empty()) {
    return error_result("compile_commands.find", file.path,
                        std::move(file.error));
  }
  try {
    const auto parsed = Json::parse(file.text);
    if (!parsed.is_array()) {
      return error_result("compile_commands.find", file.path,
                          "compile_commands.json root is not an array");
    }
    Json matches = Json::array();
    for (const auto &item : parsed) {
      if (!item.is_object()) {
        continue;
      }
      const auto source = value_string(item, "file");
      if (!args.file_contains.empty() &&
          source.find(args.file_contains) == std::string::npos) {
        continue;
      }
      matches.push_back(
          Json{{"file", source},
               {"directory", value_string(item, "directory")},
               {"command", value_string(item, "command")},
               {"arguments", item.value("arguments", Json::array())}});
      if (matches.size() >= static_cast<std::size_t>(args.max_results)) {
        break;
      }
    }
    Json structured = Json{{"tool", "compile_commands.find"},
                           {"path", slash_path(file.path)},
                           {"fileContains", args.file_contains},
                           {"matches", matches},
                           {"inputTruncated", file.truncated}};
    mcp::protocol::ToolResult result;
    result.content.push_back(
        mcp::protocol::ContentBlock::text_content(structured.dump(2)));
    result.structured_content = std::move(structured);
    return result;
  } catch (const std::exception &ex) {
    return error_result("compile_commands.find", file.path, ex.what());
  }
}

Json read_only_annotations() { return Json{{"readOnlyHint", true}}; }

} // namespace examples

namespace mcp::protocol {
template <> struct SchemaTraits<examples::SummaryArgs> {
  static Json schema() {
    return object_schema()
        .required_property("path", JsonSchema::string())
        .optional_property("max_bytes", JsonSchema::integer())
        .optional_property("sample", JsonSchema::integer())
        .additional_properties(false)
        .build();
  }
};

template <> struct SchemaTraits<examples::FindArgs> {
  static Json schema() {
    return object_schema()
        .required_property("path", JsonSchema::string())
        .optional_property("file_contains", JsonSchema::string())
        .optional_property("max_results", JsonSchema::integer())
        .optional_property("max_bytes", JsonSchema::integer())
        .additional_properties(false)
        .build();
  }
};
} // namespace mcp::protocol

int main() {
  return mcp::ServerPeer::builder()
      .name("cxxmcp-compile-commands")
      .version("0.1.0")
      .instructions("Bounded read-only compile_commands.json inspection tools.")
      .stdio()
      .tool(mcp::server::tool<examples::SummaryArgs, mcp::protocol::ToolResult>(
                "compile_commands.summary")
                .title("Compile commands summary")
                .description("Summarize a compile_commands.json file.")
                .annotations(examples::read_only_annotations())
                .handler(examples::compile_commands_summary))
      .tool(
          mcp::server::tool<examples::FindArgs, mcp::protocol::ToolResult>(
              "compile_commands.find")
              .title("Find compile command")
              .description("Find compile command entries by source path text.")
              .annotations(examples::read_only_annotations())
              .handler(examples::compile_commands_find))
      .run();
}
