#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

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
  send_session_notification(
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
