#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

#include "cxxmcp/peer.hpp"
#include "cxxmcp/run.hpp"
#include "cxxmcp/server.hpp"

namespace examples {
namespace fs = std::filesystem;
using Json = mcp::protocol::Json;

struct DigestArgs {
  std::string path;
  int max_lines = 20000;
};

struct FindArgs {
  std::string path;
  std::string pattern;
  int max_matches = 100;
};

struct SeverityCount {
  std::string severity;
  std::int64_t count = 0;
};

struct LogLine {
  std::int64_t line = 0;
  std::string text;
};

struct DigestResult {
  std::string path;
  std::int64_t scanned_lines = 0;
  std::vector<SeverityCount> severities;
  std::vector<LogLine> samples;
};

struct FindResult {
  std::string path;
  bool truncated = false;
  std::vector<LogLine> matches;
};

void from_json(const Json& json, DigestArgs& args) {
  if (json.is_string()) {
    args.path = json.get<std::string>();
    return;
  }
  args.path = json.at("path").get<std::string>();
  args.max_lines = json.value("max_lines", 20000);
}

void from_json(const Json& json, FindArgs& args) {
  if (json.is_string()) {
    args.pattern = json.get<std::string>();
    return;
  }
  args.path = json.at("path").get<std::string>();
  args.pattern = json.at("pattern").get<std::string>();
  args.max_matches = json.value("max_matches", 100);
}

void to_json(Json& json, const SeverityCount& count) {
  json = Json{{"severity", count.severity}, {"count", count.count}};
}

void to_json(Json& json, const LogLine& line) {
  json = Json{{"line", line.line}, {"text", line.text}};
}

void to_json(Json& json, const DigestResult& result) {
  json = Json{{"path", result.path},
              {"scanned_lines", result.scanned_lines},
              {"severities", result.severities},
              {"samples", result.samples}};
}

void to_json(Json& json, const FindResult& result) {
  json = Json{{"path", result.path},
              {"truncated", result.truncated},
              {"matches", result.matches}};
}

std::string severity_for(std::string line) {
  std::transform(line.begin(), line.end(), line.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  if (line.find("FATAL") != std::string::npos) {
    return "fatal";
  }
  if (line.find("ERROR") != std::string::npos ||
      line.find("EXCEPTION") != std::string::npos) {
    return "error";
  }
  if (line.find("WARN") != std::string::npos) {
    return "warning";
  }
  if (line.find("INFO") != std::string::npos) {
    return "info";
  }
  if (line.find("DEBUG") != std::string::npos ||
      line.find("TRACE") != std::string::npos) {
    return "debug";
  }
  return "unknown";
}

fs::path resolve_path(const std::string& value) {
  std::error_code ec;
  auto path = fs::path(value);
  if (path.is_relative()) {
    path = fs::absolute(path, ec);
  }
  const auto canonical = fs::weakly_canonical(path, ec);
  return ec ? path.lexically_normal() : canonical;
}

DigestResult digest_log(DigestArgs args) {
  args.max_lines = std::clamp(args.max_lines, 1, 500000);
  const auto path = resolve_path(args.path);
  DigestResult result;
  result.path = path.generic_string();
  std::ifstream input(path);
  if (!input) {
    result.samples.push_back(LogLine{0, "unable to open log file"});
    return result;
  }

  std::unordered_map<std::string, std::int64_t> counts;
  std::string line;
  while (result.scanned_lines < args.max_lines && std::getline(input, line)) {
    ++result.scanned_lines;
    auto severity = severity_for(line);
    ++counts[severity];
    if ((severity == "fatal" || severity == "error" ||
         severity == "warning") &&
        result.samples.size() < 20) {
      if (line.size() > 260) {
        line.resize(260);
      }
      result.samples.push_back(LogLine{result.scanned_lines, line});
    }
  }

  for (const auto& item : counts) {
    result.severities.push_back(SeverityCount{item.first, item.second});
  }
  std::sort(result.severities.begin(), result.severities.end(),
            [](const SeverityCount& left, const SeverityCount& right) {
              return left.count > right.count;
            });
  return result;
}

FindResult find_log(FindArgs args) {
  args.max_matches = std::clamp(args.max_matches, 1, 2000);
  const auto path = resolve_path(args.path);
  FindResult result;
  result.path = path.generic_string();
  std::ifstream input(path);
  if (!input) {
    result.matches.push_back(LogLine{0, "unable to open log file"});
    return result;
  }
  std::regex expression(args.pattern, std::regex::icase);
  std::string line;
  std::int64_t line_no = 0;
  while (std::getline(input, line)) {
    ++line_no;
    if (!std::regex_search(line, expression)) {
      continue;
    }
    if (line.size() > 260) {
      line.resize(260);
    }
    result.matches.push_back(LogLine{line_no, line});
    if (static_cast<int>(result.matches.size()) >= args.max_matches) {
      result.truncated = true;
      break;
    }
  }
  return result;
}

}  // namespace examples

namespace mcp::protocol {
template <>
struct SchemaTraits<examples::DigestArgs> {
  static Json schema() {
    return object_schema()
        .required_property("path", JsonSchema::string())
        .optional_property("max_lines", JsonSchema::integer())
        .additional_properties(false)
        .build();
  }
};

template <>
struct SchemaTraits<examples::FindArgs> {
  static Json schema() {
    return object_schema()
        .required_property("path", JsonSchema::string())
        .required_property("pattern", JsonSchema::string())
        .optional_property("max_matches", JsonSchema::integer())
        .additional_properties(false)
        .build();
  }
};

template <>
struct SchemaTraits<examples::DigestResult> {
  static Json schema() { return JsonSchema::object(); }
};

template <>
struct SchemaTraits<examples::FindResult> {
  static Json schema() { return JsonSchema::object(); }
};
}  // namespace mcp::protocol

int main() {
  return mcp::ServerPeer::builder()
      .name("cxxmcp-log-triage")
      .version("0.1.0")
      .instructions("Log and incident triage tools for local diagnostics.")
      .stdio()
      .task_manager(mcp::server::TaskOperationProcessorOptions{
          .worker_count = 2,
          .queue_size = 32,
          .poll_interval = std::int64_t{1},
      })
      .tool(mcp::server::tool<examples::DigestArgs,
                              examples::DigestResult>("logs.digest")
                .title("Digest log")
                .description("Count severity levels and return key samples.")
                .task_support(mcp::protocol::TaskSupport::Optional)
                .annotations(examples::Json{{"readOnlyHint", true}})
                .handler(examples::digest_log))
      .tool(mcp::server::tool<examples::FindArgs,
                              examples::FindResult>("logs.find")
                .title("Find log lines")
                .description("Extract matching log lines with line numbers.")
                .task_support(mcp::protocol::TaskSupport::Optional)
                .annotations(examples::Json{{"readOnlyHint", true}})
                .handler(examples::find_log))
      .prompt(
          mcp::protocol::Prompt{
              .name = "incident.report",
              .description = "Render a practical incident triage report.",
              .arguments = {mcp::protocol::PromptArgument{
                  .name = "symptom",
                  .description = "Observed failure symptom.",
                  .required = true,
              }},
          },
          [](const mcp::server::PromptContext& context) {
            const auto symptom =
                context.arguments.value("symptom", std::string{"failure"});
            mcp::protocol::PromptsGetResult result;
            result.description = "Incident report prompt";
            result.messages.push_back(mcp::protocol::PromptMessage{
                .role = "user",
                .content = mcp::protocol::ContentBlock::text_content(
                    "Prepare an incident report for: " + symptom +
                    ". Use logs.digest first, then logs.find for the highest "
                    "severity signals. Include impact, timeline, suspected "
                    "root cause, immediate mitigation, and follow-up checks."),
            });
            return result;
          })
      .resource("incident://playbook", [] {
        return mcp::protocol::ResourceContents{
            .uri = "incident://playbook",
            .mime_type = "text/markdown",
            .text =
                "# Incident triage playbook\n"
                "1. Confirm customer impact and blast radius.\n"
                "2. Identify first fatal/error line and preceding warnings.\n"
                "3. Separate trigger, root cause, and recovery signal.\n"
                "4. Prefer reversible mitigation before permanent fixes.\n",
        };
      })
      .completion([](const examples::Json& request) {
        const auto prefix = request.value("prefix", std::string{});
        const std::vector<std::string> candidates{
            "ERROR", "WARN", "FATAL", "timeout", "exception",
            "connection", "logs.digest", "logs.find", "incident.report"};
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
                            {"text", "Incident sample response for " +
                                         request.value("prompt",
                                                       std::string{"triage"})}}},
            {"model", "cxxmcp-log-triage-sample"},
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
                                         {"server", "cxxmcp-log-triage"}});
        }
        return std::nullopt;
      })
      .run();
}
