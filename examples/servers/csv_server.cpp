#include <algorithm>
#include <cstdint>
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
  std::string delimiter = ",";
  int max_rows = 10000;
  int max_bytes = 4 * 1024 * 1024;
};

struct SampleArgs {
  std::string path;
  std::string delimiter = ",";
  int rows = 20;
  int max_bytes = 1024 * 1024;
};

struct ReadResult {
  fs::path path;
  std::string text;
  bool truncated = false;
  std::string error;
};

void from_json(const Json &json, SummaryArgs &args) {
  args.path = json.at("path").get<std::string>();
  args.delimiter = json.value("delimiter", args.delimiter);
  args.max_rows = json.value("max_rows", args.max_rows);
  args.max_bytes = json.value("max_bytes", args.max_bytes);
}

void from_json(const Json &json, SampleArgs &args) {
  args.path = json.at("path").get<std::string>();
  args.delimiter = json.value("delimiter", args.delimiter);
  args.rows = json.value("rows", args.rows);
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
  const auto limit = std::clamp(max_bytes, 1, 16 * 1024 * 1024);
  result.text.assign(static_cast<std::size_t>(limit), '\0');
  input.read(result.text.data(),
             static_cast<std::streamsize>(result.text.size()));
  result.text.resize(static_cast<std::size_t>(input.gcount()));
  result.truncated = input.peek() != std::ifstream::traits_type::eof();
  return result;
}

char delimiter_char(const std::string &delimiter) {
  return delimiter.empty() ? ',' : delimiter.front();
}

std::vector<std::string> parse_csv_line(const std::string &line,
                                        char delimiter) {
  std::vector<std::string> fields;
  std::string field;
  bool quoted = false;
  for (std::size_t i = 0; i < line.size(); ++i) {
    const char ch = line[i];
    if (quoted) {
      if (ch == '"' && i + 1 < line.size() && line[i + 1] == '"') {
        field += '"';
        ++i;
      } else if (ch == '"') {
        quoted = false;
      } else {
        field += ch;
      }
      continue;
    }
    if (ch == '"') {
      quoted = true;
    } else if (ch == delimiter) {
      fields.push_back(std::move(field));
      field.clear();
    } else {
      field += ch;
    }
  }
  fields.push_back(std::move(field));
  return fields;
}

std::vector<std::string> split_lines(const std::string &text, int max_rows,
                                     bool *row_limited) {
  *row_limited = false;
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
        if (lines.size() >= static_cast<std::size_t>(max_rows)) {
          *row_limited = true;
          return lines;
        }
      }
      continue;
    }
    line += ch;
  }
  if (!line.empty() && lines.size() < static_cast<std::size_t>(max_rows)) {
    lines.push_back(std::move(line));
  }
  return lines;
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

mcp::protocol::ToolResult csv_summary(SummaryArgs args) {
  args.max_rows = std::clamp(args.max_rows, 1, 100000);
  args.max_bytes = std::clamp(args.max_bytes, 1, 16 * 1024 * 1024);
  auto file = read_bounded(args.path, args.max_bytes);
  if (!file.error.empty()) {
    return error_result("csv.summary", file.path, std::move(file.error));
  }
  bool row_limited = false;
  auto lines = split_lines(file.text, args.max_rows, &row_limited);
  const auto delimiter = delimiter_char(args.delimiter);
  std::vector<std::string> headers;
  std::vector<std::int64_t> non_empty;
  std::size_t max_columns = 0;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    auto fields = parse_csv_line(lines[i], delimiter);
    max_columns = std::max(max_columns, fields.size());
    if (i == 0) {
      headers = fields;
      non_empty.assign(fields.size(), 0);
      continue;
    }
    if (non_empty.size() < fields.size()) {
      non_empty.resize(fields.size(), 0);
    }
    for (std::size_t col = 0; col < fields.size(); ++col) {
      if (!fields[col].empty()) {
        ++non_empty[col];
      }
    }
  }
  Json columns = Json::array();
  for (std::size_t i = 0; i < max_columns; ++i) {
    const auto name = i < headers.size() && !headers[i].empty()
                          ? headers[i]
                          : "column_" + std::to_string(i + 1);
    columns.push_back(
        Json{{"index", i},
             {"name", name},
             {"nonEmpty", i < non_empty.size() ? non_empty[i] : 0}});
  }
  Json structured =
      Json{{"tool", "csv.summary"},
           {"path", slash_path(file.path)},
           {"delimiter", std::string(1, delimiter)},
           {"rowsScanned", lines.size()},
           {"dataRowsScanned", lines.empty() ? 0 : lines.size() - 1},
           {"columns", columns},
           {"inputTruncated", file.truncated},
           {"rowLimited", row_limited}};
  mcp::protocol::ToolResult result;
  result.content.push_back(
      mcp::protocol::ContentBlock::text_content(structured.dump(2)));
  result.structured_content = std::move(structured);
  return result;
}

mcp::protocol::ToolResult csv_sample(SampleArgs args) {
  args.rows = std::clamp(args.rows, 1, 1000);
  args.max_bytes = std::clamp(args.max_bytes, 1, 16 * 1024 * 1024);
  auto file = read_bounded(args.path, args.max_bytes);
  if (!file.error.empty()) {
    return error_result("csv.sample", file.path, std::move(file.error));
  }
  bool row_limited = false;
  auto lines = split_lines(file.text, args.rows, &row_limited);
  const auto delimiter = delimiter_char(args.delimiter);
  Json rows = Json::array();
  for (const auto &line : lines) {
    rows.push_back(parse_csv_line(line, delimiter));
  }
  Json structured = Json{{"tool", "csv.sample"},
                         {"path", slash_path(file.path)},
                         {"delimiter", std::string(1, delimiter)},
                         {"rows", rows},
                         {"inputTruncated", file.truncated},
                         {"rowLimited", row_limited}};
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
        .optional_property("delimiter", JsonSchema::string())
        .optional_property("max_rows", JsonSchema::integer())
        .optional_property("max_bytes", JsonSchema::integer())
        .additional_properties(false)
        .build();
  }
};

template <> struct SchemaTraits<examples::SampleArgs> {
  static Json schema() {
    return object_schema()
        .required_property("path", JsonSchema::string())
        .optional_property("delimiter", JsonSchema::string())
        .optional_property("rows", JsonSchema::integer())
        .optional_property("max_bytes", JsonSchema::integer())
        .additional_properties(false)
        .build();
  }
};
} // namespace mcp::protocol

int main() {
  return mcp::ServerPeer::builder()
      .name("cxxmcp-csv")
      .version("0.1.0")
      .instructions("Bounded read-only CSV inspection tools.")
      .stdio()
      .tool(mcp::server::tool<examples::SummaryArgs, mcp::protocol::ToolResult>(
                "csv.summary")
                .title("CSV summary")
                .description(
                    "Summarize rows and columns from a bounded CSV file.")
                .annotations(examples::read_only_annotations())
                .handler(examples::csv_summary))
      .tool(mcp::server::tool<examples::SampleArgs, mcp::protocol::ToolResult>(
                "csv.sample")
                .title("CSV sample")
                .description("Read a bounded sample of CSV rows.")
                .annotations(examples::read_only_annotations())
                .handler(examples::csv_sample))
      .run();
}
