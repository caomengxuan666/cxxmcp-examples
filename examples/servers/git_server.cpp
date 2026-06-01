#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <utility>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

#include "cxxmcp/peer.hpp"
#include "cxxmcp/run.hpp"
#include "cxxmcp/server.hpp"

namespace examples {
namespace fs = std::filesystem;
using Json = mcp::protocol::Json;

struct StatusArgs {
  std::string repo;
  int max_bytes = 64 * 1024;
};

struct LogArgs {
  std::string repo;
  int max_commits = 20;
  int max_bytes = 64 * 1024;
};

struct DiffArgs {
  std::string repo;
  int max_bytes = 128 * 1024;
};

struct CommandResult {
  std::string output;
  int exit_code = 0;
  bool truncated = false;
};

void from_json(const Json &json, StatusArgs &args) {
  args.repo = json.at("repo").get<std::string>();
  args.max_bytes = json.value("max_bytes", args.max_bytes);
}

void from_json(const Json &json, LogArgs &args) {
  args.repo = json.at("repo").get<std::string>();
  args.max_commits = json.value("max_commits", args.max_commits);
  args.max_bytes = json.value("max_bytes", args.max_bytes);
}

void from_json(const Json &json, DiffArgs &args) {
  args.repo = json.at("repo").get<std::string>();
  args.max_bytes = json.value("max_bytes", args.max_bytes);
}

std::string slash_path(const fs::path &path) {
  auto value = path.generic_string();
  return value.empty() ? std::string{"."} : value;
}

fs::path resolve_repo(const std::string &requested) {
  std::error_code ec;
  fs::path repo = requested.empty() ? fs::path{"."} : fs::path{requested};
  if (repo.is_relative()) {
    repo = fs::absolute(repo, ec);
  }
  const auto canonical = fs::weakly_canonical(repo, ec);
  return ec ? repo.lexically_normal() : canonical;
}

std::string shell_quote(const std::string &value) {
#if defined(_WIN32)
  std::string quoted = "\"";
  for (char ch : value) {
    if (ch == '"') {
      quoted += "\\\"";
    } else {
      quoted += ch;
    }
  }
  quoted += "\"";
  return quoted;
#else
  std::string quoted = "'";
  for (char ch : value) {
    if (ch == '\'') {
      quoted += "'\\''";
    } else {
      quoted += ch;
    }
  }
  quoted += "'";
  return quoted;
#endif
}

int decode_exit_code(int status) {
#if defined(_WIN32)
  return status;
#else
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  return status;
#endif
}

CommandResult run_command(const std::string &command, int max_bytes) {
  const auto limit =
      static_cast<std::size_t>(std::clamp(max_bytes, 1, 1024 * 1024));
  CommandResult result;
  std::array<char, 4096> buffer{};

#if defined(_WIN32)
  FILE *pipe = _popen(command.c_str(), "r");
#else
  FILE *pipe = popen(command.c_str(), "r");
#endif
  if (pipe == nullptr) {
    result.exit_code = -1;
    result.output = "failed to launch git";
    return result;
  }

  while (true) {
    const auto read = std::fread(buffer.data(), 1, buffer.size(), pipe);
    if (read > 0) {
      const auto remaining =
          result.output.size() < limit ? limit - result.output.size() : 0;
      if (remaining > 0) {
        result.output.append(buffer.data(), std::min(read, remaining));
      }
      if (read > remaining) {
        result.truncated = true;
      }
    }
    if (read < buffer.size()) {
      if (std::feof(pipe) != 0) {
        break;
      }
      if (std::ferror(pipe) != 0) {
        result.truncated = true;
        break;
      }
    }
  }

#if defined(_WIN32)
  result.exit_code = decode_exit_code(_pclose(pipe));
#else
  result.exit_code = decode_exit_code(pclose(pipe));
#endif
  return result;
}

mcp::protocol::ToolResult make_result(std::string tool, const fs::path &repo,
                                      CommandResult command) {
  Json structured = Json{{"tool", std::move(tool)},
                         {"repo", slash_path(repo)},
                         {"exitCode", command.exit_code},
                         {"truncated", command.truncated},
                         {"output", command.output}};
  auto text =
      command.output.empty() ? std::string{"<no output>"} : command.output;
  if (command.truncated) {
    text += "\n[truncated]";
  }

  mcp::protocol::ToolResult result;
  result.content.push_back(
      mcp::protocol::ContentBlock::text_content(std::move(text)));
  result.structured_content = std::move(structured);
  result.is_error = command.exit_code != 0;
  return result;
}

mcp::protocol::ToolResult error_result(std::string tool, const fs::path &repo,
                                       std::string message) {
  mcp::protocol::ToolResult result =
      mcp::protocol::ToolResult::error_text(message);
  result.structured_content = Json{{"tool", std::move(tool)},
                                   {"repo", slash_path(repo)},
                                   {"error", std::move(message)}};
  return result;
}

bool valid_repo_path(const fs::path &repo, std::string *message) {
  std::error_code ec;
  if (!fs::exists(repo, ec)) {
    *message = "repo path does not exist";
    return false;
  }
  if (!fs::is_directory(repo, ec)) {
    *message = "repo path is not a directory";
    return false;
  }
  return true;
}

mcp::protocol::ToolResult git_status(StatusArgs args) {
  args.max_bytes = std::clamp(args.max_bytes, 1, 1024 * 1024);
  const auto repo = resolve_repo(args.repo);
  std::string message;
  if (!valid_repo_path(repo, &message)) {
    return error_result("git.status", repo, std::move(message));
  }
  const auto command = "git --no-pager --no-optional-locks -C " +
                       shell_quote(repo.string()) +
                       " status --short --branch 2>&1";
  return make_result("git.status", repo, run_command(command, args.max_bytes));
}

mcp::protocol::ToolResult git_log(LogArgs args) {
  args.max_commits = std::clamp(args.max_commits, 1, 200);
  args.max_bytes = std::clamp(args.max_bytes, 1, 1024 * 1024);
  const auto repo = resolve_repo(args.repo);
  std::string message;
  if (!valid_repo_path(repo, &message)) {
    return error_result("git.log", repo, std::move(message));
  }
  std::ostringstream command;
  command << "git --no-pager --no-optional-locks -C "
          << shell_quote(repo.string())
          << " log --date=iso-strict --max-count=" << args.max_commits
          << " --pretty=format:%H%x09%aI%x09%an%x09%s 2>&1";
  return make_result("git.log", repo,
                     run_command(command.str(), args.max_bytes));
}

mcp::protocol::ToolResult git_diff(DiffArgs args) {
  args.max_bytes = std::clamp(args.max_bytes, 1, 1024 * 1024);
  const auto repo = resolve_repo(args.repo);
  std::string message;
  if (!valid_repo_path(repo, &message)) {
    return error_result("git.diff", repo, std::move(message));
  }
  const auto command = "git --no-pager --no-optional-locks -C " +
                       shell_quote(repo.string()) + " diff --no-ext-diff 2>&1";
  return make_result("git.diff", repo, run_command(command, args.max_bytes));
}

Json read_only_annotations() { return Json{{"readOnlyHint", true}}; }

} // namespace examples

namespace mcp::protocol {
template <> struct SchemaTraits<examples::StatusArgs> {
  static Json schema() {
    return object_schema()
        .required_property("repo", JsonSchema::string())
        .optional_property("max_bytes", JsonSchema::integer())
        .additional_properties(false)
        .build();
  }
};

template <> struct SchemaTraits<examples::LogArgs> {
  static Json schema() {
    return object_schema()
        .required_property("repo", JsonSchema::string())
        .optional_property("max_commits", JsonSchema::integer())
        .optional_property("max_bytes", JsonSchema::integer())
        .additional_properties(false)
        .build();
  }
};

template <> struct SchemaTraits<examples::DiffArgs> {
  static Json schema() {
    return object_schema()
        .required_property("repo", JsonSchema::string())
        .optional_property("max_bytes", JsonSchema::integer())
        .additional_properties(false)
        .build();
  }
};
} // namespace mcp::protocol

int main() {
  return mcp::ServerPeer::builder()
      .name("cxxmcp-git")
      .version("0.1.0")
      .instructions("Lightweight read-only Git inspection tools.")
      .stdio()
      .tool(mcp::server::tool<examples::StatusArgs, mcp::protocol::ToolResult>(
                "git.status")
                .title("Git status")
                .description("Run a bounded read-only git status.")
                .annotations(examples::read_only_annotations())
                .handler(examples::git_status))
      .tool(mcp::server::tool<examples::LogArgs, mcp::protocol::ToolResult>(
                "git.log")
                .title("Git log")
                .description("Run a bounded read-only git log.")
                .annotations(examples::read_only_annotations())
                .handler(examples::git_log))
      .tool(mcp::server::tool<examples::DiffArgs, mcp::protocol::ToolResult>(
                "git.diff")
                .title("Git diff")
                .description("Run a bounded read-only git diff.")
                .annotations(examples::read_only_annotations())
                .handler(examples::git_diff))
      .run();
}
