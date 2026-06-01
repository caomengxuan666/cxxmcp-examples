#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "cxxmcp/peer.hpp"
#include "cxxmcp/run.hpp"
#include "cxxmcp/server.hpp"

namespace examples {
namespace fs = std::filesystem;
using Json = mcp::protocol::Json;

struct SummaryArgs {
  std::string path;
  int max_lines = 10000;
  int max_bytes = 16 * 1024 * 1024;
  int max_errors = 20;
};

struct SampleArgs {
  std::string path;
  int lines = 20;
  int max_bytes = 4 * 1024 * 1024;
};

struct ReadResult {
  fs::path path;
  std::string text;
  bool truncated = false;
  std::string error;
};

void from_json(const Json &json, SummaryArgs &args) {
  args.path = json.at("path").get<std::string>();
  args.max_lines = json.value("max_lines", args.max_lines);
  args.max_bytes = json.value("max_bytes", args.max_bytes);
  args.max_errors = json.value("max_errors", args.max_errors);
}

void from_json(const Json &json, SampleArgs &args) {
  args.path = json.at("path").get<std::string>();
  args.lines = json.value("lines", args.lines);
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

std::vector<std::string> split_lines(const std::string &text, int max_lines,
                                     bool *line_limited) {
  *line_limited = false;
  std::vector<std::string> lines;
  std::string line;
  for (char ch : text) {
    if (ch == '\r') {
      continue;
    }
    if (ch == '\n') {
      if (!line.empty()) {
        lines.push_back(std::move(line));
        line.clear();
        if (lines.size() >= static_cast<std::size_t>(max_lines)) {
          *line_limited = true;
          return lines;
        }
      }
      continue;
    }
    line += ch;
  }
  if (!line.empty() && lines.size() < static_cast<std::size_t>(max_lines)) {
    lines.push_back(std::move(line));
  }
  return lines;
}

std::string json_type(const Json &value) {
  if (value.is_object()) {
    return "object";
  }
  if (value.is_array()) {
    return "array";
  }
  if (value.is_string()) {
    return "string";
  }
  if (value.is_boolean()) {
    return "boolean";
  }
  if (value.is_number()) {
    return "number";
  }
  if (value.is_null()) {
    return "null";
  }
  return "unknown";
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

mcp::protocol::ToolResult jsonl_summary(SummaryArgs args) {
  args.max_lines = std::clamp(args.max_lines, 1, 100000);
  args.max_bytes = std::clamp(args.max_bytes, 1, 64 * 1024 * 1024);
  args.max_errors = std::clamp(args.max_errors, 0, 100);
  auto file = read_bounded(args.path, args.max_bytes);
  if (!file.error.empty()) {
    return error_result("jsonl.summary", file.path, std::move(file.error));
  }
  bool line_limited = false;
  const auto lines = split_lines(file.text, args.max_lines, &line_limited);
  Json type_counts = Json::object();
  Json key_counts = Json::object();
  Json errors = Json::array();
  std::size_t valid = 0;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    try {
      const auto parsed = Json::parse(lines[i]);
      ++valid;
      const auto type = json_type(parsed);
      type_counts[type] = type_counts.value(type, 0) + 1;
      if (parsed.is_object()) {
        for (const auto &item : parsed.items()) {
          key_counts[item.key()] = key_counts.value(item.key(), 0) + 1;
        }
      }
    } catch (const std::exception &ex) {
      if (errors.size() < static_cast<std::size_t>(args.max_errors)) {
        errors.push_back(Json{{"line", i + 1}, {"error", ex.what()}});
      }
    }
  }
  Json structured = Json{{"tool", "jsonl.summary"},
                         {"path", slash_path(file.path)},
                         {"linesScanned", lines.size()},
                         {"validLines", valid},
                         {"invalidLines", lines.size() - valid},
                         {"types", type_counts},
                         {"keys", key_counts},
                         {"errors", errors},
                         {"inputTruncated", file.truncated},
                         {"lineLimited", line_limited}};
  mcp::protocol::ToolResult result;
  result.content.push_back(
      mcp::protocol::ContentBlock::text_content(structured.dump(2)));
  result.structured_content = std::move(structured);
  return result;
}

mcp::protocol::ToolResult jsonl_sample(SampleArgs args) {
  args.lines = std::clamp(args.lines, 1, 1000);
  args.max_bytes = std::clamp(args.max_bytes, 1, 64 * 1024 * 1024);
  auto file = read_bounded(args.path, args.max_bytes);
  if (!file.error.empty()) {
    return error_result("jsonl.sample", file.path, std::move(file.error));
  }
  bool line_limited = false;
  const auto lines = split_lines(file.text, args.lines, &line_limited);
  Json rows = Json::array();
  for (std::size_t i = 0; i < lines.size(); ++i) {
    try {
      rows.push_back(Json{{"line", i + 1}, {"value", Json::parse(lines[i])}});
    } catch (const std::exception &ex) {
      rows.push_back(Json{{"line", i + 1}, {"error", ex.what()}});
    }
  }
  Json structured = Json{{"tool", "jsonl.sample"},
                         {"path", slash_path(file.path)},
                         {"rows", rows},
                         {"inputTruncated", file.truncated},
                         {"lineLimited", line_limited}};
  mcp::protocol::ToolResult result;
  result.content.push_back(
      mcp::protocol::ContentBlock::text_content(structured.dump(2)));
  result.structured_content = std::move(structured);
  return result;
}

Json read_only_annotations() { return Json{{"readOnlyHint", true}}; }

} // namespace examples

namespace mcp::protocol {
template <> struct SchemaTraits<examples::SummaryArgs> {
  static Json schema() {
    return object_schema()
        .required_property("path", JsonSchema::string())
        .optional_property("max_lines", JsonSchema::integer())
        .optional_property("max_bytes", JsonSchema::integer())
        .optional_property("max_errors", JsonSchema::integer())
        .additional_properties(false)
        .build();
  }
};

template <> struct SchemaTraits<examples::SampleArgs> {
  static Json schema() {
    return object_schema()
        .required_property("path", JsonSchema::string())
        .optional_property("lines", JsonSchema::integer())
        .optional_property("max_bytes", JsonSchema::integer())
        .additional_properties(false)
        .build();
  }
};
} // namespace mcp::protocol

int main() {
  return mcp::ServerPeer::builder()
      .name("cxxmcp-jsonl")
      .version("0.1.0")
      .instructions("Bounded read-only JSON Lines inspection tools.")
      .stdio()
      .tool(mcp::server::tool<examples::SummaryArgs, mcp::protocol::ToolResult>(
                "jsonl.summary")
                .title("JSONL summary")
                .description("Summarize bounded JSON Lines data.")
                .annotations(examples::read_only_annotations())
                .handler(examples::jsonl_summary))
      .tool(mcp::server::tool<examples::SampleArgs, mcp::protocol::ToolResult>(
                "jsonl.sample")
                .title("JSONL sample")
                .description("Read a bounded sample from JSON Lines data.")
                .annotations(examples::read_only_annotations())
                .handler(examples::jsonl_sample))
      .run();
}
