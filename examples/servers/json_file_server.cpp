#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
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
  int max_bytes = 1024 * 1024;
  int max_children = 50;
};

struct PointerArgs {
  std::string path;
  std::string pointer;
  int max_bytes = 1024 * 1024;
  int max_output_bytes = 128 * 1024;
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
  args.max_children = json.value("max_children", args.max_children);
}

void from_json(const Json &json, PointerArgs &args) {
  args.path = json.at("path").get<std::string>();
  args.pointer = json.value("pointer", std::string{});
  args.max_bytes = json.value("max_bytes", args.max_bytes);
  args.max_output_bytes = json.value("max_output_bytes", args.max_output_bytes);
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
  const auto limit = std::clamp(max_bytes, 1, 8 * 1024 * 1024);
  result.text.assign(static_cast<std::size_t>(limit), '\0');
  input.read(result.text.data(),
             static_cast<std::streamsize>(result.text.size()));
  result.text.resize(static_cast<std::size_t>(input.gcount()));
  result.truncated = input.peek() != std::ifstream::traits_type::eof();
  return result;
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

std::size_t count_nodes(const Json &value) {
  std::size_t total = 1;
  if (value.is_object()) {
    for (const auto &item : value.items()) {
      total += count_nodes(item.value());
    }
  } else if (value.is_array()) {
    for (const auto &item : value) {
      total += count_nodes(item);
    }
  }
  return total;
}

std::string unescape_pointer_token(std::string token) {
  std::string out;
  out.reserve(token.size());
  for (std::size_t i = 0; i < token.size(); ++i) {
    if (token[i] == '~' && i + 1 < token.size()) {
      if (token[i + 1] == '0') {
        out += '~';
        ++i;
        continue;
      }
      if (token[i + 1] == '1') {
        out += '/';
        ++i;
        continue;
      }
    }
    out += token[i];
  }
  return out;
}

bool non_negative_integer(const std::string &text) {
  return !text.empty() &&
         std::all_of(text.begin(), text.end(),
                     [](unsigned char ch) { return std::isdigit(ch) != 0; });
}

const Json *resolve_pointer(const Json &root, const std::string &pointer,
                            std::string *error) {
  if (pointer.empty()) {
    return &root;
  }
  if (pointer.front() != '/') {
    *error = "JSON pointer must be empty or start with '/'";
    return nullptr;
  }
  const Json *current = &root;
  std::size_t start = 1;
  while (start <= pointer.size()) {
    const auto slash = pointer.find('/', start);
    const auto raw = pointer.substr(
        start, slash == std::string::npos ? slash : slash - start);
    const auto token = unescape_pointer_token(raw);
    if (current->is_object()) {
      const auto it = current->find(token);
      if (it == current->end()) {
        *error = "object key not found: " + token;
        return nullptr;
      }
      current = &(*it);
    } else if (current->is_array()) {
      if (!non_negative_integer(token)) {
        *error = "array token is not a non-negative integer: " + token;
        return nullptr;
      }
      const auto index = static_cast<std::size_t>(std::stoull(token));
      if (index >= current->size()) {
        *error = "array index out of range: " + token;
        return nullptr;
      }
      current = &((*current)[index]);
    } else {
      *error = "cannot traverse into " + json_type(*current);
      return nullptr;
    }
    if (slash == std::string::npos) {
      break;
    }
    start = slash + 1;
  }
  return current;
}

std::string bounded_dump(const Json &value, int max_output_bytes,
                         bool *truncated) {
  auto text = value.dump(2);
  const auto limit = std::clamp(max_output_bytes, 1, 1024 * 1024);
  if (text.size() > static_cast<std::size_t>(limit)) {
    text.resize(static_cast<std::size_t>(limit));
    *truncated = true;
  } else {
    *truncated = false;
  }
  return text;
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

mcp::protocol::ToolResult json_summary(SummaryArgs args) {
  args.max_bytes = std::clamp(args.max_bytes, 1, 8 * 1024 * 1024);
  args.max_children = std::clamp(args.max_children, 1, 500);
  auto file = read_bounded(args.path, args.max_bytes);
  if (!file.error.empty()) {
    return error_result("json.summary", file.path, std::move(file.error));
  }
  try {
    const auto parsed = Json::parse(file.text);
    Json children = Json::array();
    if (parsed.is_object()) {
      int count = 0;
      for (const auto &item : parsed.items()) {
        if (count++ >= args.max_children) {
          break;
        }
        children.push_back(
            Json{{"key", item.key()}, {"type", json_type(item.value())}});
      }
    } else if (parsed.is_array()) {
      const auto limit = std::min<std::size_t>(
          parsed.size(), static_cast<std::size_t>(args.max_children));
      for (std::size_t i = 0; i < limit; ++i) {
        children.push_back(Json{{"index", i}, {"type", json_type(parsed[i])}});
      }
    }
    Json structured = Json{{"tool", "json.summary"},
                           {"path", slash_path(file.path)},
                           {"type", json_type(parsed)},
                           {"size", parsed.is_structured() ? parsed.size() : 1},
                           {"nodes", count_nodes(parsed)},
                           {"inputTruncated", file.truncated},
                           {"children", children}};
    mcp::protocol::ToolResult result;
    result.content.push_back(
        mcp::protocol::ContentBlock::text_content(structured.dump(2)));
    result.structured_content = std::move(structured);
    return result;
  } catch (const std::exception &ex) {
    return error_result("json.summary", file.path, ex.what());
  }
}

mcp::protocol::ToolResult json_pointer(PointerArgs args) {
  args.max_bytes = std::clamp(args.max_bytes, 1, 8 * 1024 * 1024);
  args.max_output_bytes = std::clamp(args.max_output_bytes, 1, 1024 * 1024);
  auto file = read_bounded(args.path, args.max_bytes);
  if (!file.error.empty()) {
    return error_result("json.pointer", file.path, std::move(file.error));
  }
  try {
    const auto parsed = Json::parse(file.text);
    std::string error;
    const auto *value = resolve_pointer(parsed, args.pointer, &error);
    if (value == nullptr) {
      return error_result("json.pointer", file.path, std::move(error));
    }
    bool output_truncated = false;
    auto text = bounded_dump(*value, args.max_output_bytes, &output_truncated);
    Json structured = Json{{"tool", "json.pointer"},
                           {"path", slash_path(file.path)},
                           {"pointer", args.pointer},
                           {"type", json_type(*value)},
                           {"outputTruncated", output_truncated},
                           {"value", *value}};
    mcp::protocol::ToolResult result;
    result.content.push_back(
        mcp::protocol::ContentBlock::text_content(std::move(text)));
    result.structured_content = std::move(structured);
    return result;
  } catch (const std::exception &ex) {
    return error_result("json.pointer", file.path, ex.what());
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
        .optional_property("max_children", JsonSchema::integer())
        .additional_properties(false)
        .build();
  }
};

template <> struct SchemaTraits<examples::PointerArgs> {
  static Json schema() {
    return object_schema()
        .required_property("path", JsonSchema::string())
        .optional_property("pointer", JsonSchema::string())
        .optional_property("max_bytes", JsonSchema::integer())
        .optional_property("max_output_bytes", JsonSchema::integer())
        .additional_properties(false)
        .build();
  }
};
} // namespace mcp::protocol

int main() {
  return mcp::ServerPeer::builder()
      .name("cxxmcp-json-file")
      .version("0.1.0")
      .instructions("Bounded read-only JSON file inspection tools.")
      .stdio()
      .tool(mcp::server::tool<examples::SummaryArgs, mcp::protocol::ToolResult>(
                "json.summary")
                .title("JSON summary")
                .description("Summarize a bounded JSON file.")
                .annotations(examples::read_only_annotations())
                .handler(examples::json_summary))
      .tool(mcp::server::tool<examples::PointerArgs, mcp::protocol::ToolResult>(
                "json.pointer")
                .title("JSON pointer")
                .description("Read a value from a JSON file by JSON pointer.")
                .annotations(examples::read_only_annotations())
                .handler(examples::json_pointer))
      .run();
}
