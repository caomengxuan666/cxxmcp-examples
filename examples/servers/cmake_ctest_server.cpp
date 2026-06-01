#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
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

struct PresetsArgs {
  std::string source_dir;
  int max_bytes = 256 * 1024;
};

struct ListTestsArgs {
  std::string build_dir;
  int max_bytes = 256 * 1024;
};

struct RunTestsArgs {
  std::string build_dir;
  std::string regex;
  bool allow_run = false;
  int timeout_seconds = 60;
  int max_bytes = 512 * 1024;
};

struct CommandResult {
  std::string output;
  int exit_code = 0;
  bool truncated = false;
};

void from_json(const Json &json, PresetsArgs &args) {
  args.source_dir = json.at("source_dir").get<std::string>();
  args.max_bytes = json.value("max_bytes", args.max_bytes);
}

void from_json(const Json &json, ListTestsArgs &args) {
  args.build_dir = json.at("build_dir").get<std::string>();
  args.max_bytes = json.value("max_bytes", args.max_bytes);
}

void from_json(const Json &json, RunTestsArgs &args) {
  args.build_dir = json.at("build_dir").get<std::string>();
  args.regex = json.value("regex", std::string{});
  args.allow_run = json.value("allow_run", false);
  args.timeout_seconds = json.value("timeout_seconds", args.timeout_seconds);
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
    result.output = "failed to launch command";
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

std::string read_bounded(const fs::path &path, int max_bytes, bool *truncated) {
  *truncated = false;
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return {};
  }
  const auto limit = std::clamp(max_bytes, 1, 1024 * 1024);
  std::string text(static_cast<std::size_t>(limit), '\0');
  input.read(text.data(), static_cast<std::streamsize>(text.size()));
  text.resize(static_cast<std::size_t>(input.gcount()));
  *truncated = input.peek() != std::ifstream::traits_type::eof();
  return text;
}

mcp::protocol::ToolResult make_result(std::string tool, const fs::path &path,
                                      CommandResult command) {
  Json structured = Json{{"tool", std::move(tool)},
                         {"path", slash_path(path)},
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

mcp::protocol::ToolResult make_text_result(std::string tool,
                                           const fs::path &path,
                                           std::string text, bool truncated) {
  Json structured = Json{{"tool", std::move(tool)},
                         {"path", slash_path(path)},
                         {"truncated", truncated},
                         {"text", text}};
  if (text.empty()) {
    text = "<no output>";
  }
  if (truncated) {
    text += "\n[truncated]";
  }
  mcp::protocol::ToolResult result;
  result.content.push_back(
      mcp::protocol::ContentBlock::text_content(std::move(text)));
  result.structured_content = std::move(structured);
  return result;
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

bool valid_directory(const fs::path &path, std::string *message) {
  std::error_code ec;
  if (!fs::exists(path, ec)) {
    *message = "directory does not exist";
    return false;
  }
  if (!fs::is_directory(path, ec)) {
    *message = "path is not a directory";
    return false;
  }
  return true;
}

mcp::protocol::ToolResult cmake_presets(PresetsArgs args) {
  args.max_bytes = std::clamp(args.max_bytes, 1, 1024 * 1024);
  const auto source_dir = resolve_path(args.source_dir);
  std::string message;
  if (!valid_directory(source_dir, &message)) {
    return error_result("cmake.presets", source_dir, std::move(message));
  }
  const auto path = source_dir / "CMakePresets.json";
  std::error_code ec;
  if (!fs::exists(path, ec)) {
    return error_result("cmake.presets", source_dir,
                        "CMakePresets.json does not exist");
  }
  bool truncated = false;
  auto text = read_bounded(path, args.max_bytes, &truncated);
  return make_text_result("cmake.presets", path, std::move(text), truncated);
}

mcp::protocol::ToolResult ctest_list(ListTestsArgs args) {
  args.max_bytes = std::clamp(args.max_bytes, 1, 1024 * 1024);
  const auto build_dir = resolve_path(args.build_dir);
  std::string message;
  if (!valid_directory(build_dir, &message)) {
    return error_result("ctest.list", build_dir, std::move(message));
  }
  const auto command =
      "ctest --test-dir " + shell_quote(build_dir.string()) + " -N 2>&1";
  return make_result("ctest.list", build_dir,
                     run_command(command, args.max_bytes));
}

mcp::protocol::ToolResult ctest_run(RunTestsArgs args) {
  args.timeout_seconds = std::clamp(args.timeout_seconds, 1, 600);
  args.max_bytes = std::clamp(args.max_bytes, 1, 1024 * 1024);
  const auto build_dir = resolve_path(args.build_dir);
  std::string message;
  if (!valid_directory(build_dir, &message)) {
    return error_result("ctest.run", build_dir, std::move(message));
  }
  if (!args.allow_run) {
    return error_result("ctest.run", build_dir,
                        "set allow_run=true to execute tests");
  }
  std::ostringstream command;
  command << "ctest --test-dir " << shell_quote(build_dir.string())
          << " --output-on-failure --timeout " << args.timeout_seconds;
  if (!args.regex.empty()) {
    command << " -R " << shell_quote(args.regex);
  }
  command << " 2>&1";
  return make_result("ctest.run", build_dir,
                     run_command(command.str(), args.max_bytes));
}

Json read_only_annotations() { return Json{{"readOnlyHint", true}}; }
Json destructive_annotations() {
  return Json{{"readOnlyHint", false}, {"destructiveHint", false}};
}

} // namespace examples

namespace mcp::protocol {
template <> struct SchemaTraits<examples::PresetsArgs> {
  static Json schema() {
    return object_schema()
        .required_property("source_dir", JsonSchema::string())
        .optional_property("max_bytes", JsonSchema::integer())
        .additional_properties(false)
        .build();
  }
};

template <> struct SchemaTraits<examples::ListTestsArgs> {
  static Json schema() {
    return object_schema()
        .required_property("build_dir", JsonSchema::string())
        .optional_property("max_bytes", JsonSchema::integer())
        .additional_properties(false)
        .build();
  }
};

template <> struct SchemaTraits<examples::RunTestsArgs> {
  static Json schema() {
    return object_schema()
        .required_property("build_dir", JsonSchema::string())
        .optional_property("regex", JsonSchema::string())
        .optional_property("allow_run", JsonSchema::boolean())
        .optional_property("timeout_seconds", JsonSchema::integer())
        .optional_property("max_bytes", JsonSchema::integer())
        .additional_properties(false)
        .build();
  }
};
} // namespace mcp::protocol

int main() {
  return mcp::ServerPeer::builder()
      .name("cxxmcp-cmake-ctest")
      .version("0.1.0")
      .instructions("Bounded CMake and CTest project inspection tools.")
      .stdio()
      .tool(mcp::server::tool<examples::PresetsArgs, mcp::protocol::ToolResult>(
                "cmake.presets")
                .title("CMake presets")
                .description("Read CMakePresets.json from a source tree.")
                .annotations(examples::read_only_annotations())
                .handler(examples::cmake_presets))
      .tool(
          mcp::server::tool<examples::ListTestsArgs, mcp::protocol::ToolResult>(
              "ctest.list")
              .title("List CTest tests")
              .description("List tests registered in a build directory.")
              .annotations(examples::read_only_annotations())
              .handler(examples::ctest_list))
      .tool(
          mcp::server::tool<examples::RunTestsArgs, mcp::protocol::ToolResult>(
              "ctest.run")
              .title("Run CTest tests")
              .description("Run selected tests when allow_run=true.")
              .annotations(examples::destructive_annotations())
              .handler(examples::ctest_run))
      .run();
}
