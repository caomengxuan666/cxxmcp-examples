#include <algorithm>
#include <array>
#include <cctype>
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

struct SchemaArgs {
  std::string database;
  int max_bytes = 128 * 1024;
};

struct TablesArgs {
  std::string database;
  int max_bytes = 32 * 1024;
};

struct QueryArgs {
  std::string database;
  std::string sql;
  int max_rows = 100;
  int max_bytes = 256 * 1024;
};

struct CommandResult {
  std::string output;
  int exit_code = 0;
  bool truncated = false;
};

void from_json(const Json &json, SchemaArgs &args) {
  args.database = json.at("database").get<std::string>();
  args.max_bytes = json.value("max_bytes", args.max_bytes);
}

void from_json(const Json &json, TablesArgs &args) {
  args.database = json.at("database").get<std::string>();
  args.max_bytes = json.value("max_bytes", args.max_bytes);
}

void from_json(const Json &json, QueryArgs &args) {
  args.database = json.at("database").get<std::string>();
  args.sql = json.at("sql").get<std::string>();
  args.max_rows = json.value("max_rows", args.max_rows);
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
    result.output = "failed to launch sqlite3";
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

std::string trim_copy(std::string value) {
  const auto not_space = [](unsigned char ch) { return std::isspace(ch) == 0; };
  value.erase(value.begin(),
              std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(),
              value.end());
  return value;
}

std::string upper_copy(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
  return value;
}

bool read_only_sql(const std::string &sql) {
  const auto trimmed = upper_copy(trim_copy(sql));
  return trimmed.rfind("SELECT", 0) == 0 || trimmed.rfind("WITH", 0) == 0 ||
         trimmed.rfind("EXPLAIN", 0) == 0;
}

std::string with_limit(std::string sql, int max_rows) {
  max_rows = std::clamp(max_rows, 1, 1000);
  const auto upper = upper_copy(sql);
  if (upper.find(" LIMIT ") != std::string::npos) {
    return sql;
  }
  return std::move(sql) + " LIMIT " + std::to_string(max_rows);
}

mcp::protocol::ToolResult
make_result(std::string tool, const fs::path &database, CommandResult command) {
  Json structured = Json{{"tool", std::move(tool)},
                         {"database", slash_path(database)},
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

mcp::protocol::ToolResult
error_result(std::string tool, const fs::path &database, std::string message) {
  mcp::protocol::ToolResult result =
      mcp::protocol::ToolResult::error_text(message);
  result.structured_content = Json{{"tool", std::move(tool)},
                                   {"database", slash_path(database)},
                                   {"error", std::move(message)}};
  return result;
}

bool valid_database(const fs::path &database, std::string *message) {
  std::error_code ec;
  if (!fs::exists(database, ec)) {
    *message = "database path does not exist";
    return false;
  }
  if (!fs::is_regular_file(database, ec)) {
    *message = "database path is not a regular file";
    return false;
  }
  return true;
}

mcp::protocol::ToolResult sqlite_schema(SchemaArgs args) {
  args.max_bytes = std::clamp(args.max_bytes, 1, 1024 * 1024);
  const auto database = resolve_path(args.database);
  std::string message;
  if (!valid_database(database, &message)) {
    return error_result("sqlite.schema", database, std::move(message));
  }
  const auto command =
      "sqlite3 -readonly " + shell_quote(database.string()) + " .schema 2>&1";
  return make_result("sqlite.schema", database,
                     run_command(command, args.max_bytes));
}

mcp::protocol::ToolResult sqlite_tables(TablesArgs args) {
  args.max_bytes = std::clamp(args.max_bytes, 1, 1024 * 1024);
  const auto database = resolve_path(args.database);
  std::string message;
  if (!valid_database(database, &message)) {
    return error_result("sqlite.tables", database, std::move(message));
  }
  const auto command =
      "sqlite3 -readonly " + shell_quote(database.string()) + " .tables 2>&1";
  return make_result("sqlite.tables", database,
                     run_command(command, args.max_bytes));
}

mcp::protocol::ToolResult sqlite_query(QueryArgs args) {
  args.max_rows = std::clamp(args.max_rows, 1, 1000);
  args.max_bytes = std::clamp(args.max_bytes, 1, 1024 * 1024);
  const auto database = resolve_path(args.database);
  std::string message;
  if (!valid_database(database, &message)) {
    return error_result("sqlite.query", database, std::move(message));
  }
  if (!read_only_sql(args.sql)) {
    return error_result(
        "sqlite.query", database,
        "only SELECT, WITH, and EXPLAIN statements are allowed");
  }
  const auto sql = with_limit(std::move(args.sql), args.max_rows);
  const auto command = "sqlite3 -readonly -json " +
                       shell_quote(database.string()) + " " + shell_quote(sql) +
                       " 2>&1";
  return make_result("sqlite.query", database,
                     run_command(command, args.max_bytes));
}

Json read_only_annotations() { return Json{{"readOnlyHint", true}}; }

} // namespace examples

namespace mcp::protocol {
template <> struct SchemaTraits<examples::SchemaArgs> {
  static Json schema() {
    return object_schema()
        .required_property("database", JsonSchema::string())
        .optional_property("max_bytes", JsonSchema::integer())
        .additional_properties(false)
        .build();
  }
};

template <> struct SchemaTraits<examples::TablesArgs> {
  static Json schema() {
    return object_schema()
        .required_property("database", JsonSchema::string())
        .optional_property("max_bytes", JsonSchema::integer())
        .additional_properties(false)
        .build();
  }
};

template <> struct SchemaTraits<examples::QueryArgs> {
  static Json schema() {
    return object_schema()
        .required_property("database", JsonSchema::string())
        .required_property("sql", JsonSchema::string())
        .optional_property("max_rows", JsonSchema::integer())
        .optional_property("max_bytes", JsonSchema::integer())
        .additional_properties(false)
        .build();
  }
};
} // namespace mcp::protocol

int main() {
  return mcp::ServerPeer::builder()
      .name("cxxmcp-sqlite")
      .version("0.1.0")
      .instructions("Read-only SQLite inspection tools backed by sqlite3.")
      .stdio()
      .tool(mcp::server::tool<examples::SchemaArgs, mcp::protocol::ToolResult>(
                "sqlite.schema")
                .title("SQLite schema")
                .description("Read SQLite schema through sqlite3 -readonly.")
                .annotations(examples::read_only_annotations())
                .handler(examples::sqlite_schema))
      .tool(mcp::server::tool<examples::TablesArgs, mcp::protocol::ToolResult>(
                "sqlite.tables")
                .title("SQLite tables")
                .description("List SQLite tables through sqlite3 -readonly.")
                .annotations(examples::read_only_annotations())
                .handler(examples::sqlite_tables))
      .tool(mcp::server::tool<examples::QueryArgs, mcp::protocol::ToolResult>(
                "sqlite.query")
                .title("SQLite query")
                .description("Run a bounded read-only SQLite query.")
                .annotations(examples::read_only_annotations())
                .handler(examples::sqlite_query))
      .run();
}
