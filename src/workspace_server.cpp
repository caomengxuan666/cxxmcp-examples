#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cxxmcp/peer.hpp"
#include "cxxmcp/run.hpp"
#include "cxxmcp/server.hpp"

namespace examples {
namespace fs = std::filesystem;
using Json = mcp::protocol::Json;

struct ScanArgs {
  std::string root;
  int max_files = 2000;
  bool include_hidden = false;
};

struct ExtensionStat {
  std::string extension;
  std::int64_t files = 0;
  std::int64_t bytes = 0;
};

struct WorkspaceSummary {
  std::string root;
  std::int64_t files = 0;
  std::int64_t directories = 0;
  std::int64_t bytes = 0;
  bool truncated = false;
  std::vector<ExtensionStat> extensions;
  std::vector<std::string> warnings;
};

struct SearchArgs {
  std::string root;
  std::string pattern;
  int max_matches = 50;
  int max_file_bytes = 512 * 1024;
  bool include_hidden = false;
};

struct SearchHit {
  std::string path;
  std::int64_t line = 0;
  std::string preview;
};

struct SearchResult {
  std::string root;
  bool truncated = false;
  std::vector<SearchHit> matches;
  std::vector<std::string> warnings;
};

struct ReadFileArgs {
  std::string root;
  std::string path;
  int max_bytes = 64 * 1024;
};

struct ReadFileResult {
  std::string path;
  std::string uri;
  std::string text;
  std::int64_t bytes = 0;
  bool truncated = false;
  std::string content_hash;
};

void from_json(const Json& json, ScanArgs& args) {
  if (json.is_number_integer()) {
    args.max_files = json.get<int>();
    return;
  }
  if (json.is_string()) {
    args.root = json.get<std::string>();
    return;
  }
  args.root = json.value("root", std::string{});
  args.max_files = json.value("max_files", 2000);
  args.include_hidden = json.value("include_hidden", false);
}

void from_json(const Json& json, SearchArgs& args) {
  if (json.is_string()) {
    args.pattern = json.get<std::string>();
    return;
  }
  args.root = json.value("root", std::string{});
  args.pattern = json.at("pattern").get<std::string>();
  args.max_matches = json.value("max_matches", 50);
  args.max_file_bytes = json.value("max_file_bytes", 512 * 1024);
  args.include_hidden = json.value("include_hidden", false);
}

void from_json(const Json& json, ReadFileArgs& args) {
  if (json.is_string()) {
    args.path = json.get<std::string>();
    return;
  }
  args.root = json.value("root", std::string{});
  args.path = json.at("path").get<std::string>();
  args.max_bytes = json.value("max_bytes", 64 * 1024);
}

void to_json(Json& json, const ExtensionStat& stat) {
  json = Json{{"extension", stat.extension},
              {"files", stat.files},
              {"bytes", stat.bytes}};
}

void to_json(Json& json, const WorkspaceSummary& summary) {
  json = Json{{"root", summary.root},
              {"files", summary.files},
              {"directories", summary.directories},
              {"bytes", summary.bytes},
              {"truncated", summary.truncated},
              {"extensions", summary.extensions},
              {"warnings", summary.warnings}};
}

void to_json(Json& json, const SearchHit& hit) {
  json = Json{{"path", hit.path}, {"line", hit.line}, {"preview", hit.preview}};
}

void to_json(Json& json, const SearchResult& result) {
  json = Json{{"root", result.root},
              {"truncated", result.truncated},
              {"matches", result.matches},
              {"warnings", result.warnings}};
}

void to_json(Json& json, const ReadFileResult& result) {
  json = Json{{"path", result.path},
              {"uri", result.uri},
              {"text", result.text},
              {"bytes", result.bytes},
              {"truncated", result.truncated},
              {"content_hash", result.content_hash}};
}

std::string slash_path(fs::path path) {
  auto value = path.generic_string();
  if (value.empty()) {
    return ".";
  }
  return value;
}

bool hidden_path(const fs::path& path) {
  for (const auto& part : path) {
    const auto text = part.generic_string();
    if (!text.empty() && text.front() == '.') {
      return true;
    }
  }
  return false;
}

fs::path resolve_root(const std::string& requested, const fs::path& fallback) {
  std::error_code ec;
  auto root = requested.empty() ? fallback : fs::path(requested);
  if (root.is_relative()) {
    root = fs::absolute(root, ec);
  }
  const auto canonical = fs::weakly_canonical(root, ec);
  return ec ? root.lexically_normal() : canonical;
}

std::string relative_to(const fs::path& path, const fs::path& root) {
  std::error_code ec;
  const auto rel = fs::relative(path, root, ec);
  return slash_path(ec ? path.filename() : rel);
}

std::string fnv1a_hex(std::string_view text) {
  std::uint64_t value = 1469598103934665603ull;
  for (unsigned char ch : text) {
    value ^= ch;
    value *= 1099511628211ull;
  }
  std::ostringstream out;
  out << std::hex << std::setw(16) << std::setfill('0') << value;
  return out.str();
}

std::optional<std::string> read_bounded(const fs::path& path, int max_bytes,
                                        bool* truncated) {
  if (truncated != nullptr) {
    *truncated = false;
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  const auto limit = std::max(1, max_bytes);
  std::string text(static_cast<std::size_t>(limit), '\0');
  input.read(text.data(), static_cast<std::streamsize>(text.size()));
  text.resize(static_cast<std::size_t>(input.gcount()));
  if (truncated != nullptr) {
    *truncated = input.peek() != std::ifstream::traits_type::eof();
  }
  return text;
}

WorkspaceSummary scan_workspace(ScanArgs args, const fs::path& default_root) {
  args.max_files = std::clamp(args.max_files, 1, 100000);
  const auto root = resolve_root(args.root, default_root);
  WorkspaceSummary summary;
  summary.root = slash_path(root);
  if (!fs::exists(root) || !fs::is_directory(root)) {
    summary.warnings.push_back("root is not a directory");
    return summary;
  }

  std::unordered_map<std::string, ExtensionStat> by_ext;
  std::error_code ec;
  fs::recursive_directory_iterator it(
      root, fs::directory_options::skip_permission_denied, ec);
  const fs::recursive_directory_iterator end;
  for (; !ec && it != end; it.increment(ec)) {
    const auto rel = relative_to(it->path(), root);
    if (!args.include_hidden && hidden_path(fs::path(rel))) {
      if (it->is_directory(ec)) {
        it.disable_recursion_pending();
      }
      continue;
    }
    if (it->is_directory(ec)) {
      ++summary.directories;
      continue;
    }
    if (!it->is_regular_file(ec)) {
      continue;
    }
    if (summary.files >= args.max_files) {
      summary.truncated = true;
      break;
    }
    ++summary.files;
    const auto size = static_cast<std::int64_t>(it->file_size(ec));
    if (!ec) {
      summary.bytes += size;
    }
    auto ext = it->path().extension().generic_string();
    if (ext.empty()) {
      ext = "<none>";
    }
    auto& stat = by_ext[ext];
    stat.extension = ext;
    ++stat.files;
    if (!ec) {
      stat.bytes += size;
    }
  }
  if (ec) {
    summary.warnings.push_back(ec.message());
  }

  for (auto& item : by_ext) {
    summary.extensions.push_back(std::move(item.second));
  }
  std::sort(summary.extensions.begin(), summary.extensions.end(),
            [](const ExtensionStat& left, const ExtensionStat& right) {
              return left.bytes > right.bytes;
            });
  if (summary.extensions.size() > 20) {
    summary.extensions.resize(20);
  }
  return summary;
}

SearchResult search_workspace(SearchArgs args, const fs::path& default_root) {
  args.max_matches = std::clamp(args.max_matches, 1, 1000);
  args.max_file_bytes = std::clamp(args.max_file_bytes, 1024, 4 * 1024 * 1024);
  const auto root = resolve_root(args.root, default_root);
  SearchResult result;
  result.root = slash_path(root);
  std::regex expression(args.pattern, std::regex::icase);

  std::error_code ec;
  fs::recursive_directory_iterator it(
      root, fs::directory_options::skip_permission_denied, ec);
  const fs::recursive_directory_iterator end;
  for (; !ec && it != end; it.increment(ec)) {
    const auto rel = relative_to(it->path(), root);
    if (!args.include_hidden && hidden_path(fs::path(rel))) {
      if (it->is_directory(ec)) {
        it.disable_recursion_pending();
      }
      continue;
    }
    if (!it->is_regular_file(ec) || it->file_size(ec) > args.max_file_bytes) {
      continue;
    }
    std::ifstream input(it->path());
    if (!input) {
      continue;
    }
    std::string line;
    std::int64_t line_no = 0;
    while (std::getline(input, line)) {
      ++line_no;
      if (!std::regex_search(line, expression)) {
        continue;
      }
      if (line.size() > 240) {
        line.resize(240);
      }
      result.matches.push_back(SearchHit{rel, line_no, line});
      if (static_cast<int>(result.matches.size()) >= args.max_matches) {
        result.truncated = true;
        return result;
      }
    }
  }
  if (ec) {
    result.warnings.push_back(ec.message());
  }
  return result;
}

ReadFileResult read_file(ReadFileArgs args, const fs::path& default_root) {
  args.max_bytes = std::clamp(args.max_bytes, 1, 1024 * 1024);
  const auto root = resolve_root(args.root, default_root);
  auto path = fs::path(args.path);
  if (path.is_relative()) {
    path = root / path;
  }
  std::error_code ec;
  const auto canonical = fs::weakly_canonical(path, ec);
  if (!ec) {
    path = canonical;
  }
  bool truncated = false;
  auto text = read_bounded(path, args.max_bytes, &truncated).value_or("");
  ReadFileResult result;
  result.path = relative_to(path, root);
  result.uri = "file://" + slash_path(path);
  result.bytes = static_cast<std::int64_t>(text.size());
  result.truncated = truncated;
  result.content_hash = fnv1a_hex(text);
  result.text = std::move(text);
  return result;
}

mcp::protocol::Prompt review_prompt() {
  return mcp::protocol::Prompt{
      .name = "workspace.review",
      .description =
          "Create a focused code review plan for a workspace or patch.",
      .arguments = {mcp::protocol::PromptArgument{
          .name = "goal",
          .description = "What the reviewer should prioritize.",
          .required = true,
      }},
  };
}

std::string environment_variable(const char* name) {
#if defined(_WIN32)
  char* value = nullptr;
  std::size_t size = 0;
  if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
    return {};
  }
  std::string result(value);
  std::free(value);
  return result;
#else
  const char* value = std::getenv(name);
  return value == nullptr ? std::string{} : std::string(value);
#endif
}

}  // namespace examples

namespace mcp::protocol {
template <>
struct SchemaTraits<examples::ScanArgs> {
  static Json schema() {
    return object_schema()
        .optional_property("root", JsonSchema::string())
        .optional_property("max_files", JsonSchema::integer())
        .optional_property("include_hidden", JsonSchema::boolean())
        .additional_properties(false)
        .build();
  }
};

template <>
struct SchemaTraits<examples::SearchArgs> {
  static Json schema() {
    return object_schema()
        .optional_property("root", JsonSchema::string())
        .required_property("pattern", JsonSchema::string())
        .optional_property("max_matches", JsonSchema::integer())
        .optional_property("max_file_bytes", JsonSchema::integer())
        .optional_property("include_hidden", JsonSchema::boolean())
        .additional_properties(false)
        .build();
  }
};

template <>
struct SchemaTraits<examples::ReadFileArgs> {
  static Json schema() {
    return object_schema()
        .optional_property("root", JsonSchema::string())
        .required_property("path", JsonSchema::string())
        .optional_property("max_bytes", JsonSchema::integer())
        .additional_properties(false)
        .build();
  }
};

template <>
struct SchemaTraits<examples::WorkspaceSummary> {
  static Json schema() { return JsonSchema::object(); }
};

template <>
struct SchemaTraits<examples::SearchResult> {
  static Json schema() { return JsonSchema::object(); }
};

template <>
struct SchemaTraits<examples::ReadFileResult> {
  static Json schema() { return JsonSchema::object(); }
};
}  // namespace mcp::protocol

int main(int argc, char** argv) {
  const auto env_root = examples::environment_variable("CXXMCP_WORKSPACE_ROOT");
  auto default_root =
      examples::fs::path(argc > 1 ? argv[1] : (env_root.empty() ? "." : env_root));
  default_root = examples::resolve_root({}, default_root);

  return mcp::ServerPeer::builder()
      .name("cxxmcp-workspace")
      .version("0.1.0")
      .instructions("Bounded workspace inspection tools for code agents.")
      .stdio()
      .task_manager(mcp::server::TaskOperationProcessorOptions{
          .worker_count = 2,
          .queue_size = 32,
          .poll_interval = std::int64_t{1},
      })
      .tool(mcp::server::tool<examples::ScanArgs,
                              examples::WorkspaceSummary>("workspace.scan")
                .title("Scan workspace")
                .description("Summarize files, bytes, and top extensions.")
                .task_support(mcp::protocol::TaskSupport::Optional)
                .annotations(examples::Json{{"readOnlyHint", true}})
                .handler([default_root](examples::ScanArgs args) {
                  return examples::scan_workspace(std::move(args),
                                                  default_root);
                }))
      .tool(mcp::server::tool<examples::SearchArgs,
                              examples::SearchResult>("workspace.search")
                .title("Search workspace")
                .description("Run a bounded case-insensitive regex search.")
                .task_support(mcp::protocol::TaskSupport::Optional)
                .annotations(examples::Json{{"readOnlyHint", true}})
                .handler([default_root](examples::SearchArgs args) {
                  return examples::search_workspace(std::move(args),
                                                    default_root);
                }))
      .tool(mcp::server::tool<examples::ReadFileArgs,
                              examples::ReadFileResult>("workspace.read_file")
                .title("Read file")
                .description("Read a bounded amount of a workspace file.")
                .task_support(mcp::protocol::TaskSupport::Optional)
                .annotations(examples::Json{{"readOnlyHint", true}})
                .handler([default_root](examples::ReadFileArgs args) {
                  return examples::read_file(std::move(args), default_root);
                }))
      .prompt(examples::review_prompt(),
              [default_root](const mcp::server::PromptContext& context) {
                const auto goal =
                    context.arguments.value("goal", std::string{"review"});
                mcp::protocol::PromptsGetResult result;
                result.description = "Workspace review prompt";
                result.messages.push_back(mcp::protocol::PromptMessage{
                    .role = "user",
                    .content = mcp::protocol::ContentBlock::text_content(
                        "Review workspace " + examples::slash_path(default_root) +
                        " with this priority: " + goal +
                        ". Use workspace.scan, workspace.search, and "
                        "workspace.read_file before making claims."),
                });
                return result;
              })
      .resource("workspace://summary", [default_root] {
        auto summary =
            examples::scan_workspace(examples::ScanArgs{}, default_root);
        mcp::protocol::Json json = summary;
        return mcp::protocol::ResourceContents{
            .uri = "workspace://summary",
            .mime_type = "application/json",
            .text = json.dump(2),
        };
      })
      .resource_template("workspace://file/{path}", [] {
        return mcp::protocol::ResourceTemplate{
            .uri_template = "workspace://file/{path}",
            .name = "Workspace file",
            .description =
                "Template advertised for files readable through "
                "workspace.read_file.",
            .mime_type = "text/plain",
        };
      })
      .completion([](const examples::Json& request) {
        const auto prefix = request.value("prefix", std::string{});
        const std::vector<std::string> candidates{
            "workspace.scan",
            "workspace.search",
            "workspace.read_file",
            "workspace.review",
            "workspace://summary",
            "workspace://file/{path}",
        };
        examples::Json values = examples::Json::array();
        for (const auto& candidate : candidates) {
          if (prefix.empty() || candidate.rfind(prefix, 0) == 0) {
            values.push_back(candidate);
          }
        }
        return examples::Json{{"completion",
                               examples::Json{{"values", values},
                                              {"total", values.size()}}}};
      })
      .sampling([](const examples::Json& request) {
        return examples::Json{
            {"role", "assistant"},
            {"content",
             examples::Json{{"type", "text"},
                            {"text", "Workspace sample response for " +
                                         request.value("prompt",
                                                       std::string{"review"})}}},
            {"model", "cxxmcp-workspace-sample"},
        };
      })
      .logging([](std::string_view level, std::string_view message) {
        (void)level;
        (void)message;
      })
      .raw_request([](const mcp::protocol::JsonRpcRequest& request)
                       -> std::optional<mcp::protocol::JsonRpcResponse> {
        if (request.method == "example/health") {
          return mcp::protocol::make_response(
              request.id, examples::Json{{"ok", true},
                                         {"server", "cxxmcp-workspace"}});
        }
        return std::nullopt;
      })
      .run();
}
