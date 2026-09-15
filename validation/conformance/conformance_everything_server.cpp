#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

#include "cxxmcp/peer.hpp"
#include "cxxmcp/transport/http_transport.hpp"
#include "cxxmcp/protocol/elicitation.hpp"
#include "cxxmcp/protocol/sampling.hpp"
#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/run.hpp"
#include "cxxmcp/service.hpp"

namespace {

using Json = mcp::protocol::Json;

// ─── MRTR helpers ──────────────────────────────────────────────────────────

constexpr char kMrtrSecret[] = "conformance-test-hmac-secret";

std::string simple_hmac(const std::string& data) {
  std::hash<std::string> hasher;
  return std::to_string(hasher(data + kMrtrSecret));
}

Json make_input_required_result(Json input_requests,
                                std::optional<std::string> request_state = std::nullopt) {
  Json result{{"resultType", "input_required"},
              {"inputRequests", std::move(input_requests)}};
  if (request_state.has_value()) {
    result["requestState"] = *request_state;
  }
  return result;
}

std::string sign_state(const std::string& data) {
  Json envelope{{"data", data}, {"sig", simple_hmac(data)}};
  return envelope.dump();
}

bool verify_state(const std::string& envelope_str, std::string& out_data) {
  try {
    auto envelope = Json::parse(envelope_str);
    if (!envelope.is_object() || !envelope.contains("data") || !envelope.contains("sig")) {
      return false;
    }
    auto data = envelope["data"].get<std::string>();
    auto sig = envelope["sig"].get<std::string>();
    if (simple_hmac(data) != sig) {
      return false;
    }
    out_data = data;
    return true;
  } catch (...) {
    return false;
  }
}

bool has_input_responses(const Json& params) {
  return params.is_object() && params.contains("inputResponses") &&
         !params["inputResponses"].is_null();
}

Json get_input_response(const Json& params, const std::string& key) {
  if (!has_input_responses(params)) return nullptr;
  const auto& ir = params["inputResponses"];
  if (!ir.is_object() || !ir.contains(key)) return nullptr;
  return ir[key];
}

constexpr char kImageBase64[] =
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8DwHwAFBQIAX8jx0gAAAABJRU5ErkJggg==";
constexpr char kAudioBase64[] =
    "UklGRiYAAABXQVZFZm10IBAAAAABAAEAQB8AAAB9AAACABAAZGF0YQIAAAA=";

int configured_port(int argc, char** argv) {
  if (argc > 1) {
    return std::stoi(argv[1]);
  }
#if defined(_WIN32)
  char* value = nullptr;
  std::size_t size = 0;
  if (_dupenv_s(&value, &size, "PORT") == 0 && value != nullptr) {
    const int port = std::stoi(value);
    std::free(value);
    return port;
  }
  if (_dupenv_s(&value, &size, "CXXMCP_EVERYTHING_PORT") == 0 &&
      value != nullptr) {
    const int port = std::stoi(value);
    std::free(value);
    return port;
  }
#else
  if (const char* value = std::getenv("PORT")) {
    return std::stoi(value);
  }
  if (const char* value = std::getenv("CXXMCP_EVERYTHING_PORT")) {
    return std::stoi(value);
  }
#endif
  return 3000;
}

mcp::protocol::ToolResult text_result(std::string text) {
  return mcp::protocol::ToolResult::text(std::move(text));
}

mcp::protocol::ToolResult error_result(std::string text) {
  return mcp::protocol::ToolResult::error_text(std::move(text));
}

mcp::protocol::ResourceContents text_resource(std::string uri,
                                              std::string text) {
  return mcp::protocol::ResourceContents{
      .uri = std::move(uri),
      .mime_type = "text/plain",
      .text = std::move(text),
  };
}

mcp::protocol::ResourcesReadResult resource_result(
    mcp::protocol::ResourceContents content) {
  mcp::protocol::ResourcesReadResult result;
  result.contents.push_back(std::move(content));
  return result;
}

mcp::protocol::ToolResult embedded_resource_result(std::string uri,
                                                   std::string text) {
  mcp::protocol::ToolResult result;
  result.is_error = false;
  result.content.push_back(mcp::protocol::ContentBlock::embedded_resource(
      text_resource(std::move(uri), std::move(text))));
  return result;
}

mcp::core::Result<mcp::core::Unit> send_session_notification(
    const mcp::server::SessionContext& context, std::string method,
    Json params) {
  if (context.transport == nullptr) {
    return mcp::core::Unit{};
  }
  return context.transport->send_notification_to_session(
      context.session_id,
      mcp::protocol::make_notification(std::move(method), std::move(params)));
}

void send_log(const mcp::server::SessionContext& context, std::string text) {
  (void)send_session_notification(
      context, std::string(mcp::protocol::LoggingMessageNotificationMethod),
      Json{{"level", "info"},
           {"logger", "conformance-test-server"},
           {"data", std::move(text)}});
}

void send_progress(const mcp::server::SessionContext& context,
                   const Json& token, double progress) {
  (void)send_session_notification(
      context, std::string(mcp::protocol::ProgressNotificationMethod),
      Json{{"progressToken", token},
           {"progress", progress},
           {"total", 100.0},
           {"message", "Completed step " + std::to_string(static_cast<int>(progress)) +
                           " of 100"}});
}

Json json_schema_2020_12() {
  return Json{
      {"$schema", "https://json-schema.org/draft/2020-12/schema"},
      {"type", "object"},
      {"$defs",
       Json{{"address",
             Json{{"$anchor", "addressDef"},
                  {"type", "object"},
                  {"properties",
                   Json{{"street", Json{{"type", "string"}}},
                        {"city", Json{{"type", "string"}}}}}}}}},
      {"properties",
       Json{{"name", Json{{"type", "string"}}},
            {"address", Json{{"$ref", "#/$defs/address"}}},
            {"contactMethod",
             Json{{"type", "string"},
                  {"enum", Json::array({"phone", "email"})}}},
            {"phone", Json{{"type", "string"}}},
            {"email", Json{{"type", "string"}}}}},
      {"allOf",
       Json::array({Json{{"anyOf",
                          Json::array({Json{{"required",
                                             Json::array({"phone"})}},
                                       Json{{"required",
                                             Json::array({"email"})}}})}}})},
      {"if",
       Json{{"properties",
             Json{{"contactMethod", Json{{"const", "phone"}}}}},
            {"required", Json::array({"contactMethod"})}}},
      {"then", Json{{"required", Json::array({"phone"})}}},
      {"else", Json{{"required", Json::array({"email"})}}},
      {"additionalProperties", false},
  };
}

Json elicitation_defaults_schema() {
  return Json{{"type", "object"},
              {"properties",
               Json{{"name",
                     Json{{"type", "string"}, {"default", "John Doe"}}},
                    {"age", Json{{"type", "integer"}, {"default", 30}}},
                    {"score",
                     Json{{"type", "number"}, {"default", 95.5}}},
                    {"status",
                     Json{{"type", "string"},
                          {"enum",
                           Json::array({"active", "inactive", "pending"})},
                          {"default", "active"}}},
                    {"verified",
                     Json{{"type", "boolean"}, {"default", true}}}}},
              {"required", Json::array()}};
}

Json elicitation_enum_schema() {
  return Json{
      {"type", "object"},
      {"properties",
       Json{
           {"untitledSingle",
            Json{{"type", "string"},
                 {"enum", Json::array({"option1", "option2", "option3"})}}},
           {"titledSingle",
            Json{{"type", "string"},
                 {"oneOf",
                  Json::array({Json{{"const", "value1"},
                                     {"title", "First Option"}},
                               Json{{"const", "value2"},
                                    {"title", "Second Option"}},
                               Json{{"const", "value3"},
                                    {"title", "Third Option"}}})}}},
           {"legacyEnum",
            Json{{"type", "string"},
                 {"enum", Json::array({"opt1", "opt2", "opt3"})},
                 {"enumNames",
                  Json::array({"Option One", "Option Two", "Option Three"})}}},
           {"untitledMulti",
            Json{{"type", "array"},
                 {"items",
                  Json{{"type", "string"},
                       {"enum",
                        Json::array({"option1", "option2", "option3"})}}}}},
           {"titledMulti",
            Json{{"type", "array"},
                 {"items",
                  Json{{"anyOf",
                        Json::array({Json{{"const", "value1"},
                                           {"title", "First Choice"}},
                                     Json{{"const", "value2"},
                                          {"title", "Second Choice"}},
                                     Json{{"const", "value3"},
                                          {"title", "Third Choice"}}})}}}}}}},
      {"required", Json::array()}};
}

std::string action_text(const Json& result) {
  return result.value("action", std::string{"unknown"});
}

mcp::protocol::ToolResult elicitation_text(std::string prefix,
                                           const Json& result) {
  return text_result(std::move(prefix) + ": action=" + action_text(result) +
                     ", content=" + result.value("content", Json::object()).dump());
}

mcp::protocol::ToolResult sampling_tool(
    const mcp::server::ToolContext& context) {
  const auto prompt = context.arguments.value("prompt", std::string{});
  const auto sampled = context.client().request(
      "sampling/createMessage",
      Json{{"messages",
            Json::array({Json{{"role", "user"},
                              {"content",
                               Json{{"type", "text"}, {"text", prompt}}}}})},
           {"maxTokens", 100}});
  if (!sampled) {
    return error_result("Sampling not supported or error: " +
                        sampled.error().message);
  }
  std::string text = "No response";
  if (sampled->contains("content") && sampled->at("content").is_object()) {
    text = sampled->at("content").value("text", text);
  }
  return text_result("LLM response: " + text);
}

mcp::protocol::ToolResult elicitation_tool(
    const mcp::server::ToolContext& context) {
  const auto message =
      context.arguments.value("message", std::string{"Please provide input"});
  const auto elicited = context.client().request(
      "elicitation/create",
      Json{{"message", message},
           {"requestedSchema",
            Json{{"type", "object"},
                 {"properties",
                  Json{{"username",
                        Json{{"type", "string"},
                             {"description", "User's response"}}},
                       {"email",
                        Json{{"type", "string"},
                             {"description", "User's email address"}}}}},
                 {"required", Json::array({"username", "email"})}}}});
  if (!elicited) {
    return error_result("Elicitation not supported or error: " +
                        elicited.error().message);
  }
  return elicitation_text("User response", *elicited);
}

mcp::protocol::ToolResult raw_elicitation_tool(
    const mcp::server::ToolContext& context, std::string message, Json schema) {
  const auto response = context.client().request(
      "elicitation/create",
      Json{{"message", std::move(message)},
           {"requestedSchema", std::move(schema)}});
  if (!response) {
    return error_result("Elicitation not supported or error: " +
                        response.error().message);
  }
  return elicitation_text("Elicitation completed", *response);
}

// ─── SEP-2243 custom-header helpers ─────────────────────────────────────

constexpr int kHeaderMismatchCode = -32020;
constexpr int kMissingClientCapabilityCode = -32021;
constexpr char kClientCapabilitiesMeta[] =
    "io.modelcontextprotocol/clientCapabilities";
constexpr char kTasksExtensionId[] = "io.modelcontextprotocol/tasks";
constexpr char kSkillsExtensionId[] = "io.modelcontextprotocol/skills";

bool ascii_iequals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    const auto la = static_cast<unsigned char>(a[i]);
    const auto lb = static_cast<unsigned char>(b[i]);
    const auto ca = (la >= 'A' && la <= 'Z') ? la - 'A' + 'a' : la;
    const auto cb = (lb >= 'A' && lb <= 'Z') ? lb - 'A' + 'a' : lb;
    if (ca != cb) return false;
  }
  return true;
}

std::optional<std::string> find_header(
    const mcp::server::SessionContext& context, std::string_view name) {
  for (const auto& [header_name, value] : context.headers) {
    if (ascii_iequals(header_name, name)) return value;
  }
  return std::nullopt;
}

// Strict Base64 per the SEP-2243 test-case table: canonical alphabet only,
// length a multiple of four, '=' padding only at the end (at most two).
std::optional<std::string> strict_base64_decode(std::string_view input) {
  static constexpr char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  if (input.empty() || input.size() % 4 != 0) {
    return std::nullopt;
  }
  std::string output;
  output.reserve(input.size() / 4 * 3);
  for (std::size_t i = 0; i < input.size(); i += 4) {
    std::uint32_t block = 0;
    int padding = 0;
    for (int j = 0; j < 4; ++j) {
      const char c = input[i + j];
      if (c == '=') {
        if (i + 4 != input.size()) return std::nullopt;
        ++padding;
        block <<= 6;
        continue;
      }
      const char* pos =
          std::find(std::begin(kAlphabet), std::end(kAlphabet) - 1, c);
      if (pos == std::end(kAlphabet) - 1 || padding > 0) return std::nullopt;
      block = (block << 6) | static_cast<std::uint32_t>(pos - kAlphabet);
    }
    if (padding > 2) return std::nullopt;
    output.push_back(static_cast<char>((block >> 16) & 0xFF));
    if (padding < 2) output.push_back(static_cast<char>((block >> 8) & 0xFF));
    if (padding < 1) output.push_back(static_cast<char>(block & 0xFF));
  }
  return output;
}

// Decodes an `=?base64?<data>?=` wrapper; anything else is a literal value.
// Returns nullopt when the wrapper is present but the payload is invalid.
std::optional<std::string> decode_mcp_param_value(std::string_view value) {
  constexpr std::string_view kPrefix = "=?base64?";
  constexpr std::string_view kSuffix = "?=";
  if (value.size() >= kPrefix.size() + kSuffix.size() &&
      value.substr(0, kPrefix.size()) == kPrefix &&
      value.substr(value.size() - kSuffix.size()) == kSuffix) {
    return strict_base64_decode(
        value.substr(kPrefix.size(), value.size() - kPrefix.size() -
                                            kSuffix.size()));
  }
  return std::string(value);
}

// ─── SHA-256 (SEP-2640 resource digests) ────────────────────────────────

class Sha256 {
 public:
  Sha256() { reset(); }

  void update(const unsigned char* data, std::size_t length) {
    for (std::size_t i = 0; i < length; ++i) {
      buffer_[buffer_len_++] = data[i];
      if (buffer_len_ == 64) {
        transform(buffer_);
        bit_length_ += 512;
        buffer_len_ = 0;
      }
    }
  }

  std::array<unsigned char, 32> finish() {
    std::uint64_t bits = bit_length_ + buffer_len_ * 8;
    buffer_[buffer_len_++] = 0x80;
    if (buffer_len_ > 56) {
      while (buffer_len_ < 64) buffer_[buffer_len_++] = 0;
      transform(buffer_);
      buffer_len_ = 0;
    }
    while (buffer_len_ < 56) buffer_[buffer_len_++] = 0;
    for (int i = 7; i >= 0; --i) {
      buffer_[buffer_len_++] = static_cast<unsigned char>(bits >> (i * 8));
    }
    transform(buffer_);
    std::array<unsigned char, 32> out{};
    for (int i = 0; i < 8; ++i) {
      out[i * 4] = static_cast<unsigned char>(state_[i] >> 24);
      out[i * 4 + 1] = static_cast<unsigned char>(state_[i] >> 16);
      out[i * 4 + 2] = static_cast<unsigned char>(state_[i] >> 8);
      out[i * 4 + 3] = static_cast<unsigned char>(state_[i]);
    }
    return out;
  }

 private:
  void reset() {
    state_ = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
              0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    buffer_len_ = 0;
    bit_length_ = 0;
  }

  static std::uint32_t rotr(std::uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
  }

  void transform(const unsigned char* block) {
    static constexpr std::uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b,
        0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
        0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7,
        0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
        0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152,
        0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
        0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
        0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
        0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
             (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
             (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
             static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
      const std::uint32_t s0 =
          rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const std::uint32_t s1 =
          rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = state_[0], b = state_[1], c = state_[2],
                  d = state_[3], e = state_[4], f = state_[5],
                  g = state_[6], h = state_[7];
    for (int i = 0; i < 64; ++i) {
      const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const std::uint32_t ch = (e & f) ^ (~e & g);
      const std::uint32_t t1 = h + s1 + ch + k[i] + w[i];
      const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t t2 = s0 + maj;
      h = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  std::array<std::uint32_t, 8> state_{};
  unsigned char buffer_[64]{};
  std::size_t buffer_len_ = 0;
  std::uint64_t bit_length_ = 0;
};

std::string sha256_hex(std::string_view data) {
  Sha256 hash;
  hash.update(reinterpret_cast<const unsigned char*>(data.data()),
              data.size());
  const auto digest = hash.finish();
  std::ostringstream out;
  for (unsigned char byte : digest) {
    out << std::hex << std::setfill('0') << std::setw(2)
        << static_cast<int>(byte);
  }
  return out.str();
}

// ─── SEP-2663 tasks v2 fixture store ────────────────────────────────────

constexpr std::int64_t kTaskTtlMs = 300000;
constexpr std::int64_t kTaskPollIntervalMs = 500;

std::string iso8601_now() {
  const auto now = std::chrono::system_clock::now();
  const auto ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          now.time_since_epoch()) %
      1000;
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &time);
#else
  gmtime_r(&time, &tm);
#endif
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << '.' << std::setfill('0')
      << std::setw(3) << ms.count() << 'Z';
  return out.str();
}

struct TaskRecord {
  std::string id;
  std::string tool;
  Json arguments = Json::object();
  std::string status = "working";
  std::string created_at;
  std::string last_updated_at;
  std::int64_t ttl_ms = kTaskTtlMs;
  Json result;
  Json error;
  Json input_requests = Json::object();
  Json input_responses = Json::object();
  std::mutex mutex;
  std::condition_variable cv;
  bool cancel_requested = false;
};

std::mutex g_tasks_mutex;
std::unordered_map<std::string, std::shared_ptr<TaskRecord>> g_tasks;
std::atomic<std::uint64_t> g_task_seq{0};

bool task_is_terminal(const std::string& status) {
  return status == "completed" || status == "failed" ||
         status == "cancelled";
}

void task_touch(TaskRecord& task) {
  task.last_updated_at = iso8601_now();
}

Json task_envelope_json(const TaskRecord& task) {
  return Json{{"taskId", task.id},
              {"status", task.status},
              {"createdAt", task.created_at},
              {"lastUpdatedAt", task.last_updated_at},
              {"ttlMs", task.ttl_ms},
              {"pollIntervalMs", kTaskPollIntervalMs}};
}

Json create_task_result_json(const TaskRecord& task) {
  Json result = task_envelope_json(task);
  result["resultType"] = "task";
  return result;
}

Json detailed_task_json(TaskRecord& task) {
  std::lock_guard lock(task.mutex);
  Json json = task_envelope_json(task);
  json["resultType"] = "complete";
  if (task.status == "completed" && !task.result.is_null()) {
    json["result"] = task.result;
  }
  if (task.status == "failed" && !task.error.is_null()) {
    json["error"] = task.error;
  }
  if (task.status == "input_required" && !task.input_requests.empty()) {
    json["inputRequests"] = task.input_requests;
  }
  return json;
}

bool task_cancel_requested(TaskRecord& task) {
  std::lock_guard lock(task.mutex);
  return task.cancel_requested || task.status == "cancelled";
}

void task_complete(TaskRecord& task, Json result) {
  std::lock_guard lock(task.mutex);
  if (task.status == "cancelled") return;
  task.status = "completed";
  task.result = std::move(result);
  task.input_requests = Json::object();
  task_touch(task);
}

void task_fail(TaskRecord& task, Json error) {
  std::lock_guard lock(task.mutex);
  if (task.status == "cancelled") return;
  task.status = "failed";
  task.error = std::move(error);
  task.input_requests = Json::object();
  task_touch(task);
}

Json task_text_content(std::string text) {
  return Json{{"content",
               Json::array({Json{{"type", "text"},
                                   {"text", std::move(text)}}})}};
}

Json task_error_content(std::string text) {
  Json result = task_text_content(std::move(text));
  result["isError"] = true;
  return result;
}

Json elicitation_input_request(std::string message, Json properties,
                               Json required) {
  return Json{{"method", "elicitation/create"},
              {"params",
               Json{{"message", std::move(message)},
                    {"requestedSchema",
                     Json{{"type", "object"},
                          {"properties", std::move(properties)},
                          {"required", std::move(required)}}}}}};
}

bool sleep_slice(TaskRecord& task, int tenths_of_second) {
  for (int i = 0; i < tenths_of_second; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (task_cancel_requested(task)) return true;
  }
  return false;
}

void mark_cancelled(TaskRecord& task) {
  std::lock_guard lock(task.mutex);
  task.status = "cancelled";
  task_touch(task);
}

// Waits until every pending input request has an answer, or the task is
// cancelled. Returns true when all inputs arrived.
bool wait_for_inputs(TaskRecord& task) {
  std::unique_lock lock(task.mutex);
  task.cv.wait(lock, [&] {
    return task.cancel_requested || task.input_requests.empty();
  });
  return !task.cancel_requested;
}

void run_task(std::shared_ptr<TaskRecord> task) {
  const std::string tool = task->tool;
  const Json args = task->arguments;

  if (tool == "slow_compute") {
    const int seconds = args.value("seconds", 0);
    const std::string label = args.value("label", std::string{});
    if (sleep_slice(*task, seconds * 10)) {
      mark_cancelled(*task);
      return;
    }
    task_complete(*task, task_text_content(
                             "Computation complete" +
                             (label.empty() ? std::string{} : ": " + label)));
    return;
  }

  if (tool == "failing_job") {
    if (sleep_slice(*task, 10)) {
      mark_cancelled(*task);
      return;
    }
    task_complete(*task,
                  task_error_content("failing_job execution error"));
    return;
  }

  if (tool == "protocol_error_job") {
    if (sleep_slice(*task, 1)) {
      mark_cancelled(*task);
      return;
    }
    task_fail(*task,
              Json{{"code", -32000},
                   {"message", "protocol_error_job internal failure"}});
    return;
  }

  if (tool == "confirm_delete") {
    const std::string filename = args.value("filename", std::string{"file"});
    {
      std::lock_guard lock(task->mutex);
      task->status = "input_required";
      task->input_requests = Json{
          {"confirm",
           elicitation_input_request(
               "Confirm deletion of " + filename,
               Json{{"confirm", Json{{"type", "boolean"}}}},
               Json::array({"confirm"}))}};
      task_touch(*task);
    }
    if (!wait_for_inputs(*task)) {
      mark_cancelled(*task);
      return;
    }
    const auto response = task->input_responses.value("confirm", Json{});
    const auto action = response.value("action", std::string{});
    task_complete(*task,
                  task_text_content(action == "accept"
                                        ? "Deleted " + filename
                                        : "Deletion declined: " + filename));
    return;
  }

  if (tool == "multi_input") {
    {
      std::lock_guard lock(task->mutex);
      task->status = "input_required";
      task->input_requests = Json{
          {"first",
           elicitation_input_request(
               "First input",
               Json{{"name", Json{{"type", "string"}}}},
               Json::array({"name"}))},
          {"second",
           elicitation_input_request(
               "Second input",
               Json{{"name", Json{{"type", "string"}}}},
               Json::array({"name"}))}};
      task_touch(*task);
    }
    if (!wait_for_inputs(*task)) {
      mark_cancelled(*task);
      return;
    }
    task_complete(*task, task_text_content("All inputs received"));
    return;
  }

  if (tool == "test_tool_with_task") {
    const std::string name = args.value("name", std::string{});
    if (sleep_slice(*task, 2)) {
      mark_cancelled(*task);
      return;
    }
    task_complete(*task,
                  task_text_content("Task result for " +
                                    (name.empty() ? "anonymous" : name)));
    return;
  }

  task_fail(*task, Json{{"code", -32000}, {"message", "unknown task tool"}});
}

std::shared_ptr<TaskRecord> start_task(std::string tool, Json arguments) {
  auto task = std::make_shared<TaskRecord>();
  task->id = "task-" + std::to_string(++g_task_seq);
  task->tool = std::move(tool);
  task->arguments = std::move(arguments);
  task->created_at = iso8601_now();
  task->last_updated_at = task->created_at;
  {
    std::lock_guard lock(g_tasks_mutex);
    g_tasks[task->id] = task;
  }
  std::thread([task] { run_task(task); }).detach();
  return task;
}

std::shared_ptr<TaskRecord> find_task(const std::string& task_id) {
  std::lock_guard lock(g_tasks_mutex);
  const auto it = g_tasks.find(task_id);
  return it == g_tasks.end() ? nullptr : it->second;
}

mcp::protocol::JsonRpcResponse tasks_error_response(
    const mcp::protocol::JsonRpcRequest& request, int code,
    std::string message) {
  return mcp::protocol::make_error_response(
      request.id, mcp::protocol::make_error(code, std::move(message)));
}

std::optional<mcp::protocol::JsonRpcResponse> handle_tasks_method(
    const mcp::protocol::JsonRpcRequest& request) {
  const auto& params = request.params;

  if (request.method == "tasks/get") {
    const auto task_id = params.value("taskId", std::string{});
    if (task_id.empty()) {
      return tasks_error_response(
          request, static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "tasks/get requires a string taskId");
    }
    const auto task = find_task(task_id);
    if (!task) {
      return tasks_error_response(
          request, static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "unknown taskId: " + task_id);
    }
    return mcp::protocol::make_response(request.id, detailed_task_json(*task));
  }

  if (request.method == "tasks/update") {
    const auto task_id = params.value("taskId", std::string{});
    if (task_id.empty()) {
      return tasks_error_response(
          request, static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "tasks/update requires a string taskId");
    }
    const auto task = find_task(task_id);
    if (!task) {
      return tasks_error_response(
          request, static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "unknown taskId: " + task_id);
    }
    const auto responses = params.value("inputResponses", Json::object());
    if (responses.is_object()) {
      std::lock_guard lock(task->mutex);
      if (!task_is_terminal(task->status)) {
        for (const auto& [key, value] : responses.items()) {
          if (task->input_requests.contains(key)) {
            task->input_responses[key] = value;
            task->input_requests.erase(key);
          }
        }
        task_touch(*task);
        task->cv.notify_all();
      }
    }
    return mcp::protocol::make_response(
        request.id, Json{{"resultType", "complete"}});
  }

  if (request.method == "tasks/cancel") {
    const auto task_id = params.value("taskId", std::string{});
    if (task_id.empty()) {
      return tasks_error_response(
          request, static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "tasks/cancel requires a string taskId");
    }
    const auto task = find_task(task_id);
    if (!task) {
      return tasks_error_response(
          request, static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "unknown taskId: " + task_id);
    }
    {
      std::lock_guard lock(task->mutex);
      if (!task_is_terminal(task->status)) {
        task->cancel_requested = true;
        task->status = "cancelled";
        task_touch(*task);
        task->cv.notify_all();
      }
    }
    return mcp::protocol::make_response(
        request.id, Json{{"resultType", "complete"}});
  }

  return std::nullopt;
}

// ─── SEP-2640 skills fixtures ───────────────────────────────────────────

constexpr char kSkillRootUri[] = "skill://cxxmcp/skills/conformance-brief";
constexpr char kSkillManifestUri[] =
    "skill://cxxmcp/skills/conformance-brief/SKILL.md";
constexpr char kSkillReferenceDirUri[] =
    "skill://cxxmcp/skills/conformance-brief/reference/";
constexpr char kSkillNotesUri[] =
    "skill://cxxmcp/skills/conformance-brief/reference/notes.txt";
constexpr char kSkillsIndexDirUri[] = "skill://cxxmcp/skills/";

constexpr char kSkillManifest[] =
    "---\n"
    "name: conformance-brief\n"
    "description: Reference skill exercised by the conformance everything "
    "server fixture.\n"
    "license: Apache-2.0\n"
    "---\n"
    "# Conformance Brief\n"
    "\n"
    "Use this skill to exercise the SEP-2640 skills surface: enumerate the "
    "catalog, fetch the entry, and read this manifest plus the bundled "
    "reference notes through resources/read.\n";

constexpr char kSkillNotes[] =
    "Reference notes bundled with the conformance-brief skill. Served "
    "through resources/read so directory listings can descend into "
    "reference/.\n";

Json skill_frontmatter() {
  return Json{{"name", "conformance-brief"},
              {"description",
               "Reference skill exercised by the conformance everything "
               "server fixture."},
              {"license", "Apache-2.0"}};
}

Json skill_entry_json() {
  const std::string manifest(kSkillManifest);
  const std::string notes(kSkillNotes);
  return Json{
      {"uri", kSkillManifestUri},
      {"frontmatter", skill_frontmatter()},
      {"resources",
       Json::array({Json{{"uri", kSkillManifestUri},
                          {"digest", "sha256:" + sha256_hex(manifest)},
                          {"size", manifest.size()}},
                     Json{{"uri", kSkillNotesUri},
                          {"digest", "sha256:" + sha256_hex(notes)},
                          {"size", notes.size()}}})}};
}

Json directory_children(std::string_view uri) {
  if (uri == kSkillRootUri || uri == std::string(kSkillRootUri) + "/") {
    return Json::array(
        {Json{{"uri", kSkillManifestUri},
               {"name", "SKILL.md"},
               {"mimeType", "text/markdown"}},
         Json{{"uri", kSkillReferenceDirUri},
               {"name", "reference"},
               {"mimeType", "inode/directory"}}});
  }
  if (uri == kSkillReferenceDirUri ||
      uri == std::string(kSkillReferenceDirUri).substr(
                            0, std::string(kSkillReferenceDirUri).size() - 1)) {
    return Json::array(
        {Json{{"uri", kSkillNotesUri},
               {"name", "notes.txt"},
               {"mimeType", "text/plain"}}});
  }
  if (uri == kSkillsIndexDirUri || uri == "skill://cxxmcp/skills") {
    return Json::array(
        {Json{{"uri", std::string(kSkillRootUri) + "/"},
               {"name", "conformance-brief"},
               {"mimeType", "inode/directory"}}});
  }
  return nullptr;
}

bool is_skill_directory_uri(std::string_view uri) {
  return !directory_children(uri).is_null();
}

std::optional<mcp::protocol::JsonRpcResponse> handle_skills_method(
    const mcp::protocol::JsonRpcRequest& request) {
  if (request.method == "skills/list") {
    return mcp::protocol::make_response(
        request.id,
        Json{{"resultType", "complete"},
             {"skills", Json::array({skill_entry_json()})},
             {"ttlMs", 0},
             {"cacheScope", "public"}});
  }

  if (request.method == "skills/get") {
    const auto uri = request.params.value("uri", std::string{});
    if (uri != kSkillManifestUri) {
      return tasks_error_response(
          request, static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "unknown skill uri: " + uri);
    }
    return mcp::protocol::make_response(
        request.id,
        Json{{"resultType", "complete"},
             {"skill", skill_entry_json()},
             {"ttlMs", 0},
             {"cacheScope", "public"}});
  }

  if (request.method == "resources/directory/read") {
    const auto uri = request.params.value("uri", std::string{});
    const auto children = directory_children(uri);
    if (children.is_null()) {
      return tasks_error_response(
          request, static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "not a directory resource: " + uri);
    }
    return mcp::protocol::make_response(
        request.id,
        Json{{"resultType", "complete"},
             {"resources", children},
             {"ttlMs", 0},
             {"cacheScope", "public"}});
  }

  return std::nullopt;
}

std::optional<mcp::protocol::JsonRpcResponse> handle_raw_request(
    const mcp::protocol::JsonRpcRequest& request,
    const mcp::server::SessionContext& context) {
  if (request.method == mcp::protocol::ToolsCallMethod &&
      request.params.value("name", std::string{}) == "test_tool_with_progress") {
    const auto meta = request.params.value("_meta", Json::object());
    const auto token = meta.value("progressToken", Json{"progress-test-1"});
    send_progress(context, token, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    send_progress(context, token, 50);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    send_progress(context, token, 100);
    return mcp::protocol::make_response(
        request.id, mcp::protocol::tool_result_to_json(
                        text_result("Progress tool executed successfully")));
  }

  if (request.method == mcp::protocol::ResourcesReadMethod) {
    const auto uri = request.params.value("uri", std::string{});
    if (uri == "test://template/123/data") {
      return mcp::protocol::make_response(
          request.id,
          Json{{"contents",
                Json::array(
                    {Json{{"uri", uri},
                          {"mimeType", "application/json"},
                          {"text",
                           R"({"id":"123","templateTest":true,"data":"Data for ID: 123"})"}}})}});
    }
    if (uri == "test://nonexistent-resource-for-conformance-testing") {
      return mcp::protocol::make_error_response(
          request.id,
          mcp::protocol::make_error(mcp::protocol::ErrorCode::InvalidParams,
                                    "Resource not found", Json{{"uri", uri}}));
    }
  }

  if (request.method == mcp::protocol::ResourcesSubscribeMethod ||
      request.method == mcp::protocol::ResourcesUnsubscribeMethod) {
    return mcp::protocol::make_response(request.id, Json::object());
  }

  // ─── MRTR tools (SEP-2322) ──────────────────────────────────────────────

  const auto tool_name = request.params.value("name", std::string{});
  const auto& params = request.params;

  // A1: Basic elicitation
  if (request.method == mcp::protocol::ToolsCallMethod &&
      tool_name == "test_input_required_result_elicitation") {
    auto user_name_resp = get_input_response(params, "user_name");
    if (!user_name_resp.is_object() ||
        user_name_resp.value("action", std::string{}) != "accept") {
      return mcp::protocol::make_response(
          request.id,
          make_input_required_result(
              Json{{"user_name",
                    Json{{"method", "elicitation/create"},
                         {"params",
                          Json{{"message", "What is your name?"},
                               {"requestedSchema",
                                Json{{"type", "object"},
                                     {"properties",
                                      Json{{"name", Json{{"type", "string"}}}}},
                                     {"required", Json::array({"name"})}}}}}}}}));
    }
    auto name = user_name_resp.value("content", Json::object()).value("name", std::string{"unknown"});
    return mcp::protocol::make_response(
        request.id,
        Json{{"content", Json::array({Json{{"type", "text"}, {"text", "Hello, " + name + "!"}}})}});
  }

  // A2: Basic sampling
  if (request.method == mcp::protocol::ToolsCallMethod &&
      tool_name == "test_input_required_result_sampling") {
    auto cap_resp = get_input_response(params, "capital_question");
    if (!cap_resp.is_object()) {
      return mcp::protocol::make_response(
          request.id,
          make_input_required_result(
              Json{{"capital_question",
                    Json{{"method", "sampling/createMessage"},
                         {"params",
                          Json{{"messages",
                                Json::array({Json{{"role", "user"},
                                                  {"content",
                                                   Json{{"type", "text"},
                                                        {"text", "What is the capital of France?"}}}}})},
                               {"maxTokens", 100}}}}}}));
    }
    std::string text = "No response";
    if (cap_resp.contains("content") && cap_resp["content"].is_object()) {
      text = cap_resp["content"].value("text", text);
    }
    return mcp::protocol::make_response(
        request.id,
        Json{{"content", Json::array({Json{{"type", "text"}, {"text", text}}})}});
  }

  // A3: Basic list roots
  if (request.method == mcp::protocol::ToolsCallMethod &&
      tool_name == "test_input_required_result_list_roots") {
    auto roots_resp = get_input_response(params, "client_roots");
    if (!roots_resp.is_object()) {
      return mcp::protocol::make_response(
          request.id,
          make_input_required_result(
              Json{{"client_roots",
                    Json{{"method", "roots/list"}, {"params", Json::object()}}}}));
    }
    std::string root_name = "unknown";
    if (roots_resp.contains("roots") && roots_resp["roots"].is_array() &&
        !roots_resp["roots"].empty()) {
      root_name = roots_resp["roots"][0].value("name", root_name);
    }
    return mcp::protocol::make_response(
        request.id,
        Json{{"content",
              Json::array({Json{{"type", "text"},
                                {"text", "Roots received: " + root_name}}})}});
  }

  // A4: Request state
  if (request.method == mcp::protocol::ToolsCallMethod &&
      tool_name == "test_input_required_result_request_state") {
    auto confirm_resp = get_input_response(params, "confirm");
    auto req_state = params.value("requestState", std::string{});
    if (!confirm_resp.is_object() || req_state.empty()) {
      return mcp::protocol::make_response(
          request.id,
          make_input_required_result(
              Json{{"confirm",
                    Json{{"method", "elicitation/create"},
                         {"params",
                          Json{{"message", "Please confirm"},
                               {"requestedSchema",
                                Json{{"type", "object"},
                                     {"properties",
                                      Json{{"ok", Json{{"type", "boolean"}}}}},
                                     {"required", Json::array({"ok"})}}}}}}}},
              sign_state("round1")));
    }
    std::string state_data;
    if (!verify_state(req_state, state_data)) {
      return mcp::protocol::make_error_response(
          request.id,
          mcp::protocol::make_error(mcp::protocol::ErrorCode::InvalidParams,
                                    "Invalid requestState"));
    }
    return mcp::protocol::make_response(
        request.id,
        Json{{"content",
              Json::array({Json{{"type", "text"}, {"text", "state-ok confirmed"}}})}});
  }

  // A5: Multiple input requests
  if (request.method == mcp::protocol::ToolsCallMethod &&
      tool_name == "test_input_required_result_multiple_inputs") {
    auto name_resp = get_input_response(params, "user_name");
    auto greeting_resp = get_input_response(params, "greeting");
    auto roots_resp = get_input_response(params, "client_roots");
    auto req_state = params.value("requestState", std::string{});
    if (!name_resp.is_object() || !greeting_resp.is_object() ||
        !roots_resp.is_object() || req_state.empty()) {
      return mcp::protocol::make_response(
          request.id,
          make_input_required_result(
              Json{{"user_name",
                    Json{{"method", "elicitation/create"},
                         {"params",
                          Json{{"message", "What is your name?"},
                               {"requestedSchema",
                                Json{{"type", "object"},
                                     {"properties",
                                      Json{{"name", Json{{"type", "string"}}}}},
                                     {"required", Json::array({"name"})}}}}}}},
                   {"greeting",
                    Json{{"method", "sampling/createMessage"},
                         {"params",
                          Json{{"messages",
                                Json::array({Json{{"role", "user"},
                                                  {"content",
                                                   Json{{"type", "text"},
                                                        {"text", "Generate a greeting"}}}}})},
                               {"maxTokens", 50}}}}},
                   {"client_roots",
                    Json{{"method", "roots/list"}, {"params", Json::object()}}}},
              sign_state("multi")));
    }
    std::string state_data;
    if (!verify_state(req_state, state_data)) {
      return mcp::protocol::make_error_response(
          request.id,
          mcp::protocol::make_error(mcp::protocol::ErrorCode::InvalidParams,
                                    "Invalid requestState"));
    }
    return mcp::protocol::make_response(
        request.id,
        Json{{"content",
              Json::array({Json{{"type", "text"}, {"text", "All inputs received"}}})}});
  }

  // A6: Multi-round — dispatch by requestState to determine the round
  if (request.method == mcp::protocol::ToolsCallMethod &&
      tool_name == "test_input_required_result_multi_round") {
    auto req_state = params.value("requestState", std::string{});
    std::string state_data;
    bool has_valid_state = verify_state(req_state, state_data);

    // Round 1: no valid requestState yet
    if (!has_valid_state) {
      return mcp::protocol::make_response(
          request.id,
          make_input_required_result(
              Json{{"step1",
                    Json{{"method", "elicitation/create"},
                         {"params",
                          Json{{"message", "Step 1: What is your name?"},
                               {"requestedSchema",
                                Json{{"type", "object"},
                                     {"properties",
                                      Json{{"name", Json{{"type", "string"}}}}},
                                     {"required", Json::array({"name"})}}}}}}}},
              sign_state("round1")));
    }

    // Round 2: state is "round1"
    if (state_data == "round1") {
      return mcp::protocol::make_response(
          request.id,
          make_input_required_result(
              Json{{"step2",
                    Json{{"method", "elicitation/create"},
                         {"params",
                          Json{{"message", "Step 2: What is your favorite color?"},
                               {"requestedSchema",
                                Json{{"type", "object"},
                                     {"properties",
                                      Json{{"color", Json{{"type", "string"}}}}},
                                     {"required", Json::array({"color"})}}}}}}}},
              sign_state("round2")));
    }

    // Round 3: state is "round2"
    if (state_data == "round2") {
      return mcp::protocol::make_response(
          request.id,
          Json{{"content",
                Json::array({Json{{"type", "text"}, {"text", "Multi-round complete"}}})}});
    }

    return mcp::protocol::make_error_response(
        request.id,
        mcp::protocol::make_error(mcp::protocol::ErrorCode::InvalidParams,
                                  "Invalid requestState"));
  }

  // A12: Tampered state
  if (request.method == mcp::protocol::ToolsCallMethod &&
      tool_name == "test_input_required_result_tampered_state") {
    auto confirm_resp = get_input_response(params, "confirm");
    auto req_state = params.value("requestState", std::string{});
    if (!confirm_resp.is_object() || req_state.empty()) {
      return mcp::protocol::make_response(
          request.id,
          make_input_required_result(
              Json{{"confirm",
                    Json{{"method", "elicitation/create"},
                         {"params",
                          Json{{"message", "Confirm action"},
                               {"requestedSchema",
                                Json{{"type", "object"},
                                     {"properties",
                                      Json{{"ok", Json{{"type", "boolean"}}}}},
                                     {"required", Json::array({"ok"})}}}}}}}},
              sign_state("tampered-test")));
    }
    std::string state_data;
    if (!verify_state(req_state, state_data)) {
      return mcp::protocol::make_error_response(
          request.id,
          mcp::protocol::make_error(mcp::protocol::ErrorCode::InvalidParams,
                                    "requestState integrity check failed"));
    }
    return mcp::protocol::make_response(
        request.id,
        Json{{"content", Json::array({Json{{"type", "text"}, {"text", "OK"}}})}});
  }

  // A13: Capability check
  if (request.method == mcp::protocol::ToolsCallMethod &&
      tool_name == "test_input_required_result_capabilities") {
    Json input_requests = Json::object();
    // Always include sampling
    input_requests["capital_question"] = Json{
        {"method", "sampling/createMessage"},
        {"params",
         Json{{"messages",
               Json::array({Json{{"role", "user"},
                                 {"content",
                                  Json{{"type", "text"},
                                       {"text", "What is the capital of France?"}}}}})},
              {"maxTokens", 100}}}};
    // Only include elicitation if client declared it
    const auto& meta = params.value("_meta", Json::object());
    const auto& client_caps =
        meta.value("io.modelcontextprotocol/clientCapabilities", Json::object());
    if (client_caps.contains("elicitation")) {
      input_requests["user_name"] = Json{
          {"method", "elicitation/create"},
          {"params",
           Json{{"message", "What is your name?"},
                {"requestedSchema",
                 Json{{"type", "object"},
                      {"properties", Json{{"name", Json{{"type", "string"}}}}},
                      {"required", Json::array({"name"})}}}}}};
    }
    return mcp::protocol::make_response(
        request.id, make_input_required_result(std::move(input_requests)));
  }

  // ─── MRTR prompts (SEP-2322) ────────────────────────────────────────────

  if (request.method == mcp::protocol::PromptsGetMethod &&
      request.params.value("name", std::string{}) == "test_input_required_result_prompt") {
    auto context_resp = get_input_response(params, "user_context");
    if (!context_resp.is_object() ||
        context_resp.value("action", std::string{}) != "accept") {
      // Return InputRequiredResult as a raw JSON response
      Json result{{"resultType", "input_required"},
                  {"inputRequests",
                   Json{{"user_context",
                         Json{{"method", "elicitation/create"},
                              {"params",
                               Json{{"message", "What context should the prompt use?"},
                                    {"requestedSchema",
                                     Json{{"type", "object"},
                                          {"properties",
                                           Json{{"context", Json{{"type", "string"}}}}},
                                          {"required", Json::array({"context"})}}}}}}}}}};
      return mcp::protocol::make_response(request.id, result);
    }
    auto ctx = context_resp.value("content", Json::object()).value("context", std::string{"default"});
    Json result{{"description", "Prompt with context: " + ctx},
                {"messages",
                 Json::array({Json{{"role", "user"},
                                   {"content",
                                    Json{{"type", "text"},
                                         {"text", "Context: " + ctx}}}}})}};
    return mcp::protocol::make_response(request.id, result);
  }

  // ─── SEP-2663 task methods (v2 surface) ───────────────────────────────

  if (request.method == "tasks/get" || request.method == "tasks/update" ||
      request.method == "tasks/cancel") {
    return handle_tasks_method(request);
  }

  // ─── SEP-2640 skills surface ──────────────────────────────────────────

  if (request.method == "skills/list" || request.method == "skills/get" ||
      request.method == "resources/directory/read") {
    return handle_skills_method(request);
  }

  // ─── SEP-2663 task-aware tools/call + diagnostic fixtures ─────────────

  if (request.method == mcp::protocol::ToolsCallMethod) {
    const auto& meta = params.value("_meta", Json::object());
    const Json client_caps =
        meta.is_object()
            ? meta.value(kClientCapabilitiesMeta, Json::object())
            : Json::object();
    const auto& extensions =
        client_caps.is_object()
            ? client_caps.value("extensions", Json::object())
            : Json::object();
    const bool has_tasks_ext =
        extensions.is_object() && extensions.contains(kTasksExtensionId);

    const auto missing_capability = [&](Json required) {
      return mcp::protocol::make_error_response(
          request.id,
          mcp::protocol::make_error(
              kMissingClientCapabilityCode,
              "missing required client capability",
              Json{{"requiredCapabilities", std::move(required)}}));
    };
    const auto complete_text = [&](std::string text) {
      return mcp::protocol::make_response(
          request.id, task_text_content(std::move(text)));
    };

    // SEP-2243 custom-header validation for the annotated fixture tool.
    if (tool_name == "test_header_echo") {
      const auto& args = params.value("arguments", Json::object());
      const std::string token = args.value("token", std::string{});
      const auto header = find_header(context, "Mcp-Param-Token");
      if (!header.has_value()) {
        return mcp::protocol::make_error_response(
            request.id,
            mcp::protocol::make_error(
                kHeaderMismatchCode,
                "missing Mcp-Param-Token header for arguments.token"));
      }
      const auto decoded = decode_mcp_param_value(*header);
      if (!decoded.has_value() || *decoded != token) {
        return mcp::protocol::make_error_response(
            request.id,
            mcp::protocol::make_error(
                kHeaderMismatchCode,
                "Mcp-Param-Token header does not match arguments.token"));
      }
      return complete_text("header echo: " + token);
    }

    if (tool_name == "test_missing_capability") {
      if (!client_caps.is_object() || !client_caps.contains("sampling")) {
        return missing_capability(Json{{"sampling", Json::object()}});
      }
      return complete_text("capability probe executed");
    }

    if (tool_name == "test_streaming_elicitation") {
      if (!client_caps.is_object() || !client_caps.contains("elicitation")) {
        return missing_capability(Json{{"elicitation", Json::object()}});
      }
      return complete_text("streaming elicitation executed");
    }

    // Never emits notifications/message — the scenario only asserts the
    // absence when _meta carries no io.modelcontextprotocol/logLevel.
    if (tool_name == "test_logging_tool") {
      return complete_text("logging tool executed");
    }

    if (tool_name == "test_trigger_tool_change") {
      if (context.transport != nullptr) {
        context.transport->publish_subscription_notification(
            "notifications/tools/list_changed", Json::object());
      }
      return complete_text("tool list change triggered");
    }

    if (tool_name == "test_trigger_prompt_change") {
      if (context.transport != nullptr) {
        context.transport->publish_subscription_notification(
            "notifications/prompts/list_changed", Json::object());
      }
      return complete_text("prompt list change triggered");
    }

    // taskSupport=required: clients without the extension get -32021.
    if (tool_name == "failing_job" || tool_name == "test_tool_with_task") {
      if (tool_name == "test_tool_with_task" && !has_input_responses(params)) {
        // Round 1 of the MRTR->tasks composition: gather the name first.
        return mcp::protocol::make_response(
            request.id,
            make_input_required_result(
                Json{{"user_name",
                      elicitation_input_request(
                          "What is your name?",
                          Json{{"name", Json{{"type", "string"}}}},
                          Json::array({"name"}))}},
                sign_state("with-task")));
      }
      if (!has_tasks_ext) {
        return missing_capability(
            Json{{"extensions",
                  Json{{kTasksExtensionId, Json::object()}}}});
      }
      Json task_args = params.value("arguments", Json::object());
      if (tool_name == "test_tool_with_task") {
        const auto name_resp = get_input_response(params, "user_name");
        task_args["name"] =
            name_resp.is_object()
                ? name_resp.value("content", Json::object())
                      .value("name", std::string{})
                : std::string{};
      }
      const auto task = start_task(tool_name, std::move(task_args));
      return mcp::protocol::make_response(request.id,
                                          create_task_result_json(*task));
    }

    // taskSupport=optional: create a task when negotiated, else fall
    // through to the synchronous registry handler.
    if (has_tasks_ext &&
        (tool_name == "slow_compute" || tool_name == "confirm_delete" ||
         tool_name == "multi_input" || tool_name == "protocol_error_job")) {
      const auto task =
          start_task(tool_name, params.value("arguments", Json::object()));
      return mcp::protocol::make_response(request.id,
                                          create_task_result_json(*task));
    }
  }

  return std::nullopt;
}

mcp::protocol::ServerCapabilities conformance_capabilities() {
  mcp::protocol::ServerCapabilities capabilities;
  capabilities.tools.enabled = true;
  capabilities.tools.list_changed = true;
  capabilities.resources.enabled = true;
  capabilities.resources.list_changed = true;
  capabilities.resources.subscribe = true;
  capabilities.resources.subscribe_present = true;
  capabilities.prompts.enabled = true;
  capabilities.prompts.list_changed = true;
  capabilities.logging.enabled = true;
  capabilities.completions.enabled = true;
  capabilities.extensions[kTasksExtensionId] = Json::object();
  capabilities.extensions[kSkillsExtensionId] =
      Json{{"directoryRead", true}};
  return capabilities;
}

mcp::protocol::Prompt prompt_definition(
    std::string name, std::string description,
    std::vector<mcp::protocol::PromptArgument> arguments = {}) {
  return mcp::protocol::Prompt{
      .name = std::move(name),
      .description = std::move(description),
      .arguments = std::move(arguments),
  };
}

}  // namespace

int main(int argc, char** argv) {
  const int port = configured_port(argc, argv);
  std::cerr << "cxxmcp everything server listening on http://127.0.0.1:"
            << port << "/mcp\n";

  return mcp::ServerPeer::builder()
          .name("mcp-conformance-test-server")
          .version("1.0.0")
          .instructions("C++ MCP everything server for conformance testing.")
          .capabilities(conformance_capabilities())
          .streamable_http("127.0.0.1", port, "/mcp")
          .add_tool(mcp::protocol::tool_definition("test_simple_text")
                        .description("Tests simple text content response")
                        .input_schema(Json{{"type", "object"}})
                        .build(),
                    [](const mcp::server::ToolContext&)
                        -> mcp::core::Result<mcp::protocol::ToolResult> {
                      return text_result(
                          "This is a simple text response for testing.");
                    })
          .add_tool(mcp::protocol::tool_definition("test_image_content")
                        .description("Tests image content response")
                        .input_schema(Json{{"type", "object"}})
                        .build(),
                    [](const mcp::server::ToolContext&)
                        -> mcp::core::Result<mcp::protocol::ToolResult> {
                      mcp::protocol::ToolResult result;
                      result.is_error = false;
                      result.content.push_back(
                          mcp::protocol::ContentBlock::image(kImageBase64,
                                                             "image/png"));
                      return result;
                    })
          .add_tool(mcp::protocol::tool_definition("test_audio_content")
                        .description("Tests audio content response")
                        .input_schema(Json{{"type", "object"}})
                        .build(),
                    [](const mcp::server::ToolContext&)
                        -> mcp::core::Result<mcp::protocol::ToolResult> {
                      mcp::protocol::ToolResult result;
                      result.is_error = false;
                      result.content.push_back(
                          mcp::protocol::ContentBlock::audio(kAudioBase64,
                                                             "audio/wav"));
                      return result;
                    })
          .add_tool(mcp::protocol::tool_definition("test_embedded_resource")
                        .description("Tests embedded resource content response")
                        .input_schema(Json{{"type", "object"}})
                        .build(),
                    [](const mcp::server::ToolContext&)
                        -> mcp::core::Result<mcp::protocol::ToolResult> {
                      return embedded_resource_result(
                          "test://embedded-resource",
                          "This is an embedded resource content.");
                    })
          .add_tool(mcp::protocol::tool_definition("test_multiple_content_types")
                        .description("Tests multiple content types")
                        .input_schema(Json{{"type", "object"}})
                        .build(),
                    [](const mcp::server::ToolContext&)
                        -> mcp::core::Result<mcp::protocol::ToolResult> {
                      mcp::protocol::ToolResult result;
                      result.is_error = false;
                      result.content.push_back(
                          mcp::protocol::ContentBlock::text_content(
                              "Multiple content types test:"));
                      result.content.push_back(
                          mcp::protocol::ContentBlock::image(kImageBase64,
                                                             "image/png"));
                      result.content.push_back(
                          mcp::protocol::ContentBlock::embedded_resource(
                              mcp::protocol::ResourceContents{
                                  .uri = "test://mixed-content-resource",
                                  .mime_type = "application/json",
                                  .text = R"({"test":"data","value":123})",
                              }));
                      return result;
                    })
          .add_tool(mcp::protocol::tool_definition("test_tool_with_logging")
                        .description("Tests log notifications during execution")
                        .input_schema(Json{{"type", "object"}})
                        .build(),
                    [](const mcp::server::ToolContext& context)
                        -> mcp::core::Result<mcp::protocol::ToolResult> {
                      send_log(context, "Tool execution started");
                      std::this_thread::sleep_for(std::chrono::milliseconds(50));
                      send_log(context, "Tool processing data");
                      std::this_thread::sleep_for(std::chrono::milliseconds(50));
                      send_log(context, "Tool execution completed");
                      return text_result(
                          "Tool with logging executed successfully");
                    })
          .add_tool(mcp::protocol::tool_definition("test_tool_with_progress")
                        .description("Tests progress notifications")
                        .input_schema(Json{{"type", "object"}})
                        .build(),
                    [](const mcp::server::ToolContext&)
                        -> mcp::core::Result<mcp::protocol::ToolResult> {
                      return text_result(
                          "Progress tool executed successfully");
                    })
          .add_tool(mcp::protocol::tool_definition("test_error_handling")
                        .description("Tests error result handling")
                        .input_schema(Json{{"type", "object"}})
                        .build(),
                    [](const mcp::server::ToolContext&)
                        -> mcp::core::Result<mcp::protocol::ToolResult> {
                      return error_result(
                          "This tool intentionally returns an error for "
                          "testing");
                    })
          .add_tool(mcp::protocol::tool_definition("test_sampling")
                        .description("Tests server-initiated sampling")
                        .input_schema(Json{{"type", "object"},
                                           {"properties",
                                            Json{{"prompt",
                                                  Json{{"type", "string"}}}}},
                                           {"required", Json::array({"prompt"})}})
                        .build(),
                    [](const mcp::server::ToolContext& context)
                        -> mcp::core::Result<mcp::protocol::ToolResult> {
                      return sampling_tool(context);
                    })
          .add_tool(mcp::protocol::tool_definition("test_elicitation")
                        .description("Tests server-initiated elicitation")
                        .input_schema(Json{{"type", "object"},
                                           {"properties",
                                            Json{{"message",
                                                  Json{{"type", "string"}}}}},
                                           {"required", Json::array({"message"})}})
                        .build(),
                    [](const mcp::server::ToolContext& context)
                        -> mcp::core::Result<mcp::protocol::ToolResult> {
                      return elicitation_tool(context);
                    })
          .add_tool(mcp::protocol::tool_definition(
                        "test_elicitation_sep1034_defaults")
                        .description("Tests elicitation default values")
                        .input_schema(Json{{"type", "object"}})
                        .build(),
                    [](const mcp::server::ToolContext& context)
                        -> mcp::core::Result<mcp::protocol::ToolResult> {
                      return raw_elicitation_tool(
                          context,
                          "Please review and update the form fields with "
                          "defaults",
                          elicitation_defaults_schema());
                    })
          .add_tool(mcp::protocol::tool_definition(
                        "test_elicitation_sep1330_enums")
                        .description("Tests elicitation enum schemas")
                        .input_schema(Json{{"type", "object"}})
                        .build(),
                    [](const mcp::server::ToolContext& context)
                        -> mcp::core::Result<mcp::protocol::ToolResult> {
                      return raw_elicitation_tool(
                          context, "Please choose enum values",
                          elicitation_enum_schema());
                    })
          .add_tool(mcp::protocol::tool_definition("json_schema_2020_12_tool")
                        .description("Tool with JSON Schema 2020-12 features")
                        .input_schema(json_schema_2020_12())
                        .build(),
                    [](const mcp::server::ToolContext&)
                        -> mcp::core::Result<mcp::protocol::ToolResult> {
                      return text_result("JSON Schema 2020-12 tool executed");
                    })
          .add_tool(mcp::protocol::tool_definition("greet")
                        .description("Sync-only greeting tool")
                        .input_schema(Json{{"type", "object"},
                                           {"properties",
                                            Json{{"name",
                                                  Json{{"type", "string"}}}}},
                                           {"required", Json::array({"name"})}})
                        .build(),
                    [](const mcp::server::ToolContext& context)
                        -> mcp::core::Result<mcp::protocol::ToolResult> {
                      return text_result(
                          "Hello, " +
                          context.arguments.value("name", std::string{"World"}) +
                          "!");
                    })
          .add_tool(mcp::protocol::tool_definition("slow_compute")
                        .description("Task-supporting tool that sleeps "
                                     "`seconds` then returns a result")
                        .input_schema(Json{{"type", "object"},
                                           {"properties",
                                            Json{{"seconds",
                                                  Json{{"type", "integer"}}},
                                                 {"label",
                                                  Json{{"type", "string"}}}}}})
                        .task_support(mcp::protocol::TaskSupport::Optional)
                        .build(),
                    [](const mcp::server::ToolContext& context)
                        -> mcp::core::Result<mcp::protocol::ToolResult> {
                      const int seconds =
                          context.arguments.value("seconds", 0);
                      for (int i = 0; i < seconds * 10; ++i) {
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(100));
                      }
                      const auto label =
                          context.arguments.value("label", std::string{});
                      return text_result(
                          "Computation complete" +
                          (label.empty() ? std::string{} : ": " + label));
                    })
          .add_tool(mcp::protocol::tool_definition("failing_job")
                        .description("Task-required tool that always reports "
                                     "a tool execution error")
                        .input_schema(Json{{"type", "object"}})
                        .task_support(mcp::protocol::TaskSupport::Required)
                        .build(),
                    [](const mcp::server::ToolContext&)
                        -> mcp::core::Result<mcp::protocol::ToolResult> {
                      return error_result("failing_job requires tasks");
                    })
          .add_tool(mcp::protocol::tool_definition("protocol_error_job")
                        .description("Task-supporting tool that surfaces a "
                                     "protocol-level failure")
                        .input_schema(Json{{"type", "object"}})
                        .task_support(mcp::protocol::TaskSupport::Optional)
                        .build(),
                    [](const mcp::server::ToolContext&)
                        -> mcp::core::Result<mcp::protocol::ToolResult> {
                      return error_result(
                          "protocol_error_job requires tasks");
                    })
          .add_tool(mcp::protocol::tool_definition("confirm_delete")
                        .description("Task-supporting tool that parks for a "
                                     "single elicitation input")
                        .input_schema(Json{{"type", "object"},
                                           {"properties",
                                            Json{{"filename",
                                                  Json{{"type", "string"}}}}}})
                        .task_support(mcp::protocol::TaskSupport::Optional)
                        .build(),
                    [](const mcp::server::ToolContext& context)
                        -> mcp::core::Result<mcp::protocol::ToolResult> {
                      return text_result(
                          "Deleted " +
                          context.arguments.value("filename",
                                                  std::string{"file"}));
                    })
          .add_tool(mcp::protocol::tool_definition("multi_input")
                        .description("Task-supporting tool that fans out two "
                                     "parallel elicitation inputs")
                        .input_schema(Json{{"type", "object"}})
                        .task_support(mcp::protocol::TaskSupport::Optional)
                        .build(),
                    [](const mcp::server::ToolContext&)
                        -> mcp::core::Result<mcp::protocol::ToolResult> {
                      return text_result("All inputs received");
                    })
          .add_tool(mcp::protocol::tool_definition("test_tool_with_task")
                        .description("MRTR round then escalates to an async "
                                     "task; result reflects gathered input")
                        .input_schema(Json{{"type", "object"}})
                        .task_support(mcp::protocol::TaskSupport::Required)
                        .build(),
                    [](const mcp::server::ToolContext&)
                        -> mcp::core::Result<mcp::protocol::ToolResult> {
                      return error_result("test_tool_with_task requires tasks");
                    })
          .add_tool(mcp::protocol::tool_definition("test_header_echo")
                        .description("Echoes an argument mirrored through the "
                                     "Mcp-Param-Token custom header")
                        .input_schema(
                            Json{{"type", "object"},
                                 {"properties",
                                  Json{{"token",
                                        Json{{"type", "string"},
                                             {"x-mcp-header", "Token"}}}}},
                                 {"required", Json::array({"token"})}})
                        .build(),
                    [](const mcp::server::ToolContext& context)
                        -> mcp::core::Result<mcp::protocol::ToolResult> {
                      return text_result(
                          "header echo: " +
                          context.arguments.value("token", std::string{}));
                    })
          .add_tool(mcp::protocol::tool_definition("test_missing_capability")
                        .description("Diagnostic tool that requires the "
                                     "sampling client capability")
                        .input_schema(Json{{"type", "object"}})
                        .build(),
                    [](const mcp::server::ToolContext&)
                        -> mcp::core::Result<mcp::protocol::ToolResult> {
                      return text_result("capability probe executed");
                    })
          .add_tool(mcp::protocol::tool_definition("test_streaming_elicitation")
                        .description("Diagnostic tool that requires the "
                                     "elicitation client capability")
                        .input_schema(Json{{"type", "object"}})
                        .build(),
                    [](const mcp::server::ToolContext&)
                        -> mcp::core::Result<mcp::protocol::ToolResult> {
                      return text_result("streaming elicitation executed");
                    })
          .add_tool(mcp::protocol::tool_definition("test_logging_tool")
                        .description("Diagnostic tool that only logs when a "
                                     "logLevel is negotiated")
                        .input_schema(Json{{"type", "object"}})
                        .build(),
                    [](const mcp::server::ToolContext&)
                        -> mcp::core::Result<mcp::protocol::ToolResult> {
                      return text_result("logging tool executed");
                    })
          .add_tool(mcp::protocol::tool_definition("test_trigger_tool_change")
                        .description("Publishes notifications/tools/"
                                     "list_changed to subscription streams")
                        .input_schema(Json{{"type", "object"}})
                        .build(),
                    [](const mcp::server::ToolContext&)
                        -> mcp::core::Result<mcp::protocol::ToolResult> {
                      return text_result("tool list change triggered");
                    })
          .add_tool(mcp::protocol::tool_definition("test_trigger_prompt_change")
                        .description("Publishes notifications/prompts/"
                                     "list_changed to subscription streams")
                        .input_schema(Json{{"type", "object"}})
                        .build(),
                    [](const mcp::server::ToolContext&)
                        -> mcp::core::Result<mcp::protocol::ToolResult> {
                      return text_result("prompt list change triggered");
                    })
          .resource(mcp::protocol::Resource{
                        .uri = "test://static-text",
                        .name = "Static text",
                        .description = "Static text conformance resource",
                        .mime_type = "text/plain",
                    },
                    [](const mcp::server::ResourceContext&)
                        -> mcp::core::Result<mcp::protocol::ResourcesReadResult> {
                      return resource_result(mcp::protocol::ResourceContents{
                          .uri = "test://static-text",
                          .mime_type = "text/plain",
                          .text = "This is the content of the static text "
                                  "resource.",
                      });
                    })
          .resource(mcp::protocol::Resource{
                        .uri = "test://static-binary",
                        .name = "Static binary",
                        .description = "Static binary conformance resource",
                        .mime_type = "image/png",
                    },
                    [](const mcp::server::ResourceContext&)
                        -> mcp::core::Result<mcp::protocol::ResourcesReadResult> {
                      return resource_result(mcp::protocol::ResourceContents{
                          .uri = "test://static-binary",
                          .mime_type = "image/png",
                          .blob = std::string(kImageBase64),
                      });
                    })
          .resource(mcp::protocol::Resource{
                        .uri = "test://watched-resource",
                        .name = "Watched resource",
                        .description = "Resource used for subscribe tests",
                        .mime_type = "text/plain",
                    },
                    [](const mcp::server::ResourceContext&)
                        -> mcp::core::Result<mcp::protocol::ResourcesReadResult> {
                      return resource_result(mcp::protocol::ResourceContents{
                          .uri = "test://watched-resource",
                          .mime_type = "text/plain",
                          .text = "Watched resource content",
                      });
                    })
          .resource(mcp::protocol::Resource{
                        .uri = "test://template/123/data",
                        .name = "Template data 123",
                        .description = "Expanded template resource",
                        .mime_type = "application/json",
                    },
                    [](const mcp::server::ResourceContext&)
                        -> mcp::core::Result<mcp::protocol::ResourcesReadResult> {
                      return resource_result(mcp::protocol::ResourceContents{
                          .uri = "test://template/123/data",
                          .mime_type = "application/json",
                          .text = R"({"id":"123","templateTest":true,"data":"Data for ID: 123"})",
                      });
                    })
          .resource(mcp::protocol::Resource{
                        .uri = kSkillsIndexDirUri,
                        .name = "skills",
                        .description = "Skill catalog root directory",
                        .mime_type = "inode/directory",
                    },
                    [](const mcp::server::ResourceContext&)
                        -> mcp::core::Result<mcp::protocol::ResourcesReadResult> {
                      return resource_result(mcp::protocol::ResourceContents{
                          .uri = kSkillsIndexDirUri,
                          .mime_type = "inode/directory",
                          .text = "Skill catalog root",
                      });
                    })
          .resource(mcp::protocol::Resource{
                        .uri = std::string(kSkillRootUri) + "/",
                        .name = "conformance-brief",
                        .description = "conformance-brief skill directory",
                        .mime_type = "inode/directory",
                    },
                    [](const mcp::server::ResourceContext&)
                        -> mcp::core::Result<mcp::protocol::ResourcesReadResult> {
                      return resource_result(mcp::protocol::ResourceContents{
                          .uri = std::string(kSkillRootUri) + "/",
                          .mime_type = "inode/directory",
                          .text = "conformance-brief skill directory",
                      });
                    })
          .resource(mcp::protocol::Resource{
                        .uri = kSkillManifestUri,
                        .name = "conformance-brief",
                        .description = "Reference skill exercised by the "
                                       "conformance everything server "
                                       "fixture.",
                        .mime_type = "text/markdown",
                    },
                    [](const mcp::server::ResourceContext&)
                        -> mcp::core::Result<mcp::protocol::ResourcesReadResult> {
                      return resource_result(mcp::protocol::ResourceContents{
                          .uri = kSkillManifestUri,
                          .mime_type = "text/markdown",
                          .text = std::string(kSkillManifest),
                      });
                    })
          .resource(mcp::protocol::Resource{
                        .uri = kSkillReferenceDirUri,
                        .name = "reference",
                        .description = "conformance-brief reference directory",
                        .mime_type = "inode/directory",
                    },
                    [](const mcp::server::ResourceContext&)
                        -> mcp::core::Result<mcp::protocol::ResourcesReadResult> {
                      return resource_result(mcp::protocol::ResourceContents{
                          .uri = kSkillReferenceDirUri,
                          .mime_type = "inode/directory",
                          .text = "conformance-brief reference directory",
                      });
                    })
          .resource(mcp::protocol::Resource{
                        .uri = kSkillNotesUri,
                        .name = "notes.txt",
                        .description = "conformance-brief reference notes",
                        .mime_type = "text/plain",
                    },
                    [](const mcp::server::ResourceContext&)
                        -> mcp::core::Result<mcp::protocol::ResourcesReadResult> {
                      return resource_result(mcp::protocol::ResourceContents{
                          .uri = kSkillNotesUri,
                          .mime_type = "text/plain",
                          .text = std::string(kSkillNotes),
                      });
                    })
          .resource_template(mcp::protocol::ResourceTemplate{
              .uri_template = "test://template/{id}/data",
              .name = "Template data",
              .description = "Template resource for conformance reads",
              .mime_type = "application/json",
          })
          .prompt(
              prompt_definition("test_simple_prompt",
                                "Simple conformance prompt"),
              [](const mcp::server::PromptContext&) {
                mcp::protocol::PromptsGetResult result;
                result.messages.push_back(mcp::protocol::PromptMessage::text(
                    "user", "This is a simple prompt for testing."));
                return result;
              })
          .prompt(
              prompt_definition(
                  "test_prompt_with_arguments",
                  "Parameterized conformance prompt",
                  {mcp::protocol::PromptArgument{.name = "arg1",
                                                 .description =
                                                     "First test argument",
                                                 .required = true},
                   mcp::protocol::PromptArgument{.name = "arg2",
                                                 .description =
                                                     "Second test argument",
                                                 .required = true}}),
              [](const mcp::server::PromptContext& context) {
                const auto arg1 =
                    context.arguments.value("arg1", std::string{});
                const auto arg2 =
                    context.arguments.value("arg2", std::string{});
                mcp::protocol::PromptsGetResult result;
                result.messages.push_back(mcp::protocol::PromptMessage::text(
                    "user", "Prompt with arguments: arg1='" + arg1 +
                                "', arg2='" + arg2 + "'"));
                return result;
              })
          .prompt(
              prompt_definition(
                  "test_prompt_with_embedded_resource",
                  "Prompt containing an embedded resource",
                  {mcp::protocol::PromptArgument{.name = "resourceUri",
                                                 .description =
                                                     "URI of resource to embed",
                                                 .required = true}}),
              [](const mcp::server::PromptContext& context) {
                const auto uri =
                    context.arguments.value("resourceUri", std::string{});
                mcp::protocol::PromptsGetResult result;
                result.messages.push_back(mcp::protocol::PromptMessage{
                    .role = "user",
                    .content =
                        mcp::protocol::ContentBlock::embedded_resource(
                            text_resource(
                                uri,
                                "Embedded resource content for testing.")),
                });
                result.messages.push_back(mcp::protocol::PromptMessage::text(
                    "user", "Please process the embedded resource above."));
                return result;
              })
          .prompt(
              prompt_definition("test_prompt_with_image",
                                "Prompt containing image content"),
              [](const mcp::server::PromptContext&) {
                mcp::protocol::PromptsGetResult result;
                result.messages.push_back(mcp::protocol::PromptMessage{
                    .role = "user",
                    .content =
                        mcp::protocol::ContentBlock::image(kImageBase64,
                                                           "image/png"),
                });
                result.messages.push_back(mcp::protocol::PromptMessage::text(
                    "user", "Please analyze the image above."));
                return result;
              })
          .completion([](const Json&) {
            return Json{{"completion",
                         Json{{"values", Json::array({"testValue1", "testValue2"})},
                              {"total", 2},
                              {"hasMore", false}}}};
          })
          .logging([](std::string_view, std::string_view) {})
          .on_raw_request(handle_raw_request)
          .run();
}
