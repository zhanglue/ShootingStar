#include "src/utilities/logger/logger.h"

#include <google/protobuf/message.h>
#include <google/protobuf/util/json_util.h>
#include <grpcpp/support/interceptor.h>

#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace shooting_star {
namespace utilities {
namespace {

using ::google::protobuf::FieldDescriptor;
using ::google::protobuf::Message;
using ::google::protobuf::Reflection;
using ::google::protobuf::util::JsonPrintOptions;
using ::google::protobuf::util::MessageToJsonString;
using ::grpc::Status;
using ::grpc::StatusCode;
using ::grpc::experimental::InterceptionHookPoints;
using ::grpc::experimental::Interceptor;
using ::grpc::experimental::InterceptorBatchMethods;
using ::grpc::experimental::ServerInterceptorFactoryInterface;
using ::grpc::experimental::ServerRpcInfo;
using ::std::initializer_list;
using ::std::int64_t;
using ::std::invalid_argument;
using ::std::lock_guard;
using ::std::make_unique;
using ::std::mutex;
using ::std::ostringstream;
using ::std::put_time;
using ::std::setfill;
using ::std::setw;
using ::std::string;
using ::std::string_view;
using ::std::time_t;
using ::std::tm;
using ::std::to_string;
using ::std::unique_ptr;
using ::std::vector;

using SteadyClock = ::std::chrono::steady_clock;
using SystemClock = ::std::chrono::system_clock;
using ::std::chrono::duration_cast;
using Milliseconds = ::std::chrono::milliseconds;

constexpr size_t kMaxPayloadJsonLength = 4096;

LogLevel ParseLogLevel(string_view value) {
  if (value == "debug" || value == "DEBUG" || value == "Debug") {
    return LogLevel::kDebug;
  }
  if (value == "info" || value == "INFO" || value == "Info") {
    return LogLevel::kInfo;
  }
  if (value == "warning" || value == "WARNING" || value == "Warning" ||
      value == "warn" || value == "WARN" || value == "Warn") {
    return LogLevel::kWarning;
  }
  if (value == "error" || value == "ERROR" || value == "Error") {
    return LogLevel::kError;
  }
  throw invalid_argument("Unsupported log level: " + string(value));
}

bool ShouldLog(LogLevel message_log_level, LogLevel min_log_level) {
  return static_cast<int>(message_log_level) >= static_cast<int>(min_log_level);
}

LogLevel LogLevelFromSeverity(string_view severity) {
  if (severity == "debug") {
    return LogLevel::kDebug;
  }
  if (severity == "warning") {
    return LogLevel::kWarning;
  }
  if (severity == "error") {
    return LogLevel::kError;
  }
  return LogLevel::kInfo;
}

class JsonLogBuilder {
 public:
  JsonLogBuilder() = default;

  void AddString(string_view key, string_view value) {
    AddKey(key);
    line_ += '"';
    line_ += EscapeJson(value);
    line_ += '"';
  }

  void AddInt(string_view key, int64_t value) {
    AddKey(key);
    line_ += to_string(value);
  }

  void AddBool(string_view key, bool value) {
    AddKey(key);
    line_ += value ? "true" : "false";
  }

  string Finish() && {
    line_ += '}';
    return ::std::move(line_);
  }

 private:
  static string EscapeJson(string_view value) {
    string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
      switch (ch) {
        case '\\':
          escaped += "\\\\";
          break;
        case '"':
          escaped += "\\\"";
          break;
        case '\b':
          escaped += "\\b";
          break;
        case '\f':
          escaped += "\\f";
          break;
        case '\n':
          escaped += "\\n";
          break;
        case '\r':
          escaped += "\\r";
          break;
        case '\t':
          escaped += "\\t";
          break;
        default:
          if (static_cast<unsigned char>(ch) < 0x20) {
            ostringstream stream;
            stream << "\\u" << std::hex << setw(4) << setfill('0')
                   << static_cast<int>(static_cast<unsigned char>(ch));
            escaped += stream.str();
          } else {
            escaped += ch;
          }
          break;
      }
    }
    return escaped;
  }

  void AddKey(string_view key) {
    if (!first_field_) {
      line_ += ',';
    }
    first_field_ = false;
    line_ += '"';
    line_ += EscapeJson(key);
    line_ += "\":";
  }

  bool first_field_ = true;
  string line_ = "{";
};

mutex& GetLogMutex() {
  static mutex mutex;
  return mutex;
}

string FormatUtcTimestamp() {
  const SystemClock::time_point now = SystemClock::now();
  const time_t seconds = SystemClock::to_time_t(now);
  tm utc_time;
  gmtime_r(&seconds, &utc_time);

  const auto milliseconds =
      duration_cast<Milliseconds>(now.time_since_epoch()) % 1000;

  ostringstream stream;
  stream << put_time(&utc_time, "%Y-%m-%dT%H:%M:%S") << '.' << setw(3)
         << setfill('0') << milliseconds.count() << 'Z';
  return stream.str();
}

string RpcTypeToString(ServerRpcInfo::Type rpc_type) {
  switch (rpc_type) {
    case ServerRpcInfo::Type::UNARY:
      return "unary";
    case ServerRpcInfo::Type::CLIENT_STREAMING:
      return "client_streaming";
    case ServerRpcInfo::Type::SERVER_STREAMING:
      return "server_streaming";
    case ServerRpcInfo::Type::BIDI_STREAMING:
      return "bidi_streaming";
  }

  return "unknown";
}

string StatusCodeToString(StatusCode status_code) {
  switch (status_code) {
    case StatusCode::OK:
      return "OK";
    case StatusCode::CANCELLED:
      return "CANCELLED";
    case StatusCode::UNKNOWN:
      return "UNKNOWN";
    case StatusCode::INVALID_ARGUMENT:
      return "INVALID_ARGUMENT";
    case StatusCode::DEADLINE_EXCEEDED:
      return "DEADLINE_EXCEEDED";
    case StatusCode::NOT_FOUND:
      return "NOT_FOUND";
    case StatusCode::ALREADY_EXISTS:
      return "ALREADY_EXISTS";
    case StatusCode::PERMISSION_DENIED:
      return "PERMISSION_DENIED";
    case StatusCode::RESOURCE_EXHAUSTED:
      return "RESOURCE_EXHAUSTED";
    case StatusCode::FAILED_PRECONDITION:
      return "FAILED_PRECONDITION";
    case StatusCode::ABORTED:
      return "ABORTED";
    case StatusCode::OUT_OF_RANGE:
      return "OUT_OF_RANGE";
    case StatusCode::UNIMPLEMENTED:
      return "UNIMPLEMENTED";
    case StatusCode::INTERNAL:
      return "INTERNAL";
    case StatusCode::UNAVAILABLE:
      return "UNAVAILABLE";
    case StatusCode::DATA_LOSS:
      return "DATA_LOSS";
    case StatusCode::UNAUTHENTICATED:
      return "UNAUTHENTICATED";
    default:
      return "UNKNOWN";
  }
}

string ExtractRequestId(const Message& message) {
  const auto* descriptor = message.GetDescriptor();
  if (descriptor == nullptr) {
    return "";
  }

  const FieldDescriptor* request_id_field =
      descriptor->FindFieldByName("request_id");
  if (request_id_field == nullptr ||
      request_id_field->cpp_type() != FieldDescriptor::CPPTYPE_STRING ||
      request_id_field->is_repeated()) {
    return "";
  }

  const Reflection* reflection = message.GetReflection();
  if (reflection == nullptr ||
      !reflection->HasField(message, request_id_field)) {
    return "";
  }

  return reflection->GetString(message, request_id_field);
}

bool TryConvertMessageToJson(const Message& message, string* json_out,
                             string* error_out) {
  if (json_out == nullptr || error_out == nullptr) {
    return false;
  }

  JsonPrintOptions options;
  options.preserve_proto_field_names = true;

  const auto status = MessageToJsonString(message, json_out, options);
  if (!status.ok()) {
    *error_out = status.ToString();
    return false;
  }

  error_out->clear();
  return true;
}

string TruncateForLog(string_view value, bool* was_truncated) {
  if (was_truncated != nullptr) {
    *was_truncated = value.size() > kMaxPayloadJsonLength;
  }

  if (value.size() <= kMaxPayloadJsonLength) {
    return string(value);
  }

  return string(value.substr(0, kMaxPayloadJsonLength));
}

void WriteLogLine(string line) {
  const lock_guard<mutex> lock(GetLogMutex());
  std::cout << line << std::endl;
}

void LogStructured(LogLevel min_log_level, string_view severity,
                   string_view service_name, string_view event,
                   initializer_list<LogField> fields) {
  if (!ShouldLog(LogLevelFromSeverity(severity), min_log_level)) {
    return;
  }

  JsonLogBuilder builder;
  builder.AddString("timestamp", FormatUtcTimestamp());
  builder.AddString("service_name", service_name);
  builder.AddString("severity", severity);
  builder.AddString("event", event);
  for (const LogField& field : fields) {
    builder.AddString(field.key, field.value);
  }
  WriteLogLine(::std::move(builder).Finish());
}

class ServerLoggingInterceptor final : public Interceptor {
 public:
  ServerLoggingInterceptor(string service_name, LogLevel min_log_level,
                           ServerRpcInfo* info)
      : service_name_(::std::move(service_name)),
        min_log_level_(min_log_level),
        server_context_(info == nullptr ? nullptr : info->server_context()),
        method_(info == nullptr || info->method() == nullptr ? ""
                                                             : info->method()),
        log_payloads_(info != nullptr &&
                      info->type() == ServerRpcInfo::Type::UNARY),
        rpc_type_(info == nullptr ? "unknown" : RpcTypeToString(info->type())),
        start_time_(SteadyClock::now()) {}

  void Intercept(InterceptorBatchMethods* methods) override {
    if (log_payloads_ && methods->QueryInterceptionHookPoint(
                             InterceptionHookPoints::POST_RECV_MESSAGE)) {
      LogRequestMessage(methods->GetRecvMessage());
    }

    if (log_payloads_ && methods->QueryInterceptionHookPoint(
                             InterceptionHookPoints::PRE_SEND_MESSAGE)) {
      LogResponseMessage(methods->GetSendMessage());
    }

    if (methods->QueryInterceptionHookPoint(
            InterceptionHookPoints::PRE_SEND_STATUS)) {
      LogCompletion(methods->GetSendStatus());
    } else if (!completion_logged_ &&
               methods->QueryInterceptionHookPoint(
                   InterceptionHookPoints::POST_RECV_CLOSE)) {
      const bool cancelled =
          server_context_ != nullptr && server_context_->IsCancelled();
      const Status status(
          cancelled ? StatusCode::CANCELLED : StatusCode::UNKNOWN,
          cancelled ? "RPC closed after cancellation."
                    : "RPC closed before final status was observed.");
      LogCompletion(status);
    }

    methods->Proceed();
  }

 private:
  void LogRequestMessage(void* recv_message) {
    if (request_logged_ || recv_message == nullptr) {
      return;
    }
    request_logged_ = true;

    const auto* message = static_cast<const Message*>(recv_message);
    if (message == nullptr) {
      return;
    }

    request_id_ = ExtractRequestId(*message);
    has_request_id_ = !request_id_.empty();
    LogPayloadEvent("request_received", "request_json", *message);
  }

  void LogResponseMessage(const void* send_message) {
    if (response_logged_ || send_message == nullptr) {
      return;
    }
    response_logged_ = true;

    const auto* message = static_cast<const Message*>(send_message);
    if (message == nullptr) {
      return;
    }

    LogPayloadEvent("response_sent", "response_json", *message);
  }

  void LogPayloadEvent(string_view event, string_view payload_field_name,
                       const Message& message) const {
    if (!ShouldLog(LogLevel::kDebug, min_log_level_)) {
      return;
    }

    string payload_json;
    string payload_error;

    JsonLogBuilder builder;
    builder.AddString("timestamp", FormatUtcTimestamp());
    builder.AddString("service_name", service_name_);
    builder.AddString("severity", "debug");
    builder.AddString("method", method_);
    if (has_request_id_) {
      builder.AddString("request_id", request_id_);
    }
    builder.AddString("event", event);
    builder.AddString("rpc_type", rpc_type_);
    builder.AddString(
        "peer", server_context_ == nullptr ? "" : server_context_->peer());

    if (!TryConvertMessageToJson(message, &payload_json, &payload_error)) {
      builder.AddString("payload_json_error", payload_error);
      WriteLogLine(::std::move(builder).Finish());
      return;
    }

    bool payload_truncated = false;
    const string payload_for_log =
        TruncateForLog(payload_json, &payload_truncated);
    builder.AddString(payload_field_name, payload_for_log);
    builder.AddBool("payload_truncated", payload_truncated);
    WriteLogLine(::std::move(builder).Finish());
  }

  void LogCompletion(const Status& status) {
    if (completion_logged_) {
      return;
    }
    completion_logged_ = true;

    const LogLevel message_log_level =
        status.ok() ? LogLevel::kInfo : LogLevel::kError;
    if (!ShouldLog(message_log_level, min_log_level_)) {
      return;
    }

    JsonLogBuilder builder;
    builder.AddString("timestamp", FormatUtcTimestamp());
    builder.AddString("service_name", service_name_);
    builder.AddString("severity", status.ok() ? "info" : "error");
    builder.AddString("method", method_);
    if (has_request_id_) {
      builder.AddString("request_id", request_id_);
    }
    builder.AddString("event", "call_completed");
    builder.AddString("rpc_type", rpc_type_);
    builder.AddString(
        "peer", server_context_ == nullptr ? "" : server_context_->peer());
    builder.AddInt(
        "latency_ms",
        duration_cast<Milliseconds>(SteadyClock::now() - start_time_).count());
    builder.AddInt("status_code", static_cast<int>(status.error_code()));
    builder.AddString("status_name", StatusCodeToString(status.error_code()));
    builder.AddString("status_message", status.error_message());
    builder.AddBool("cancelled", server_context_ != nullptr &&
                                     server_context_->IsCancelled());

    WriteLogLine(::std::move(builder).Finish());
  }

  string service_name_;
  LogLevel min_log_level_;
  ::grpc::ServerContextBase* server_context_ = nullptr;
  string method_;
  bool log_payloads_ = false;
  string rpc_type_;
  string request_id_;
  SteadyClock::time_point start_time_;
  bool completion_logged_ = false;
  bool has_request_id_ = false;
  bool request_logged_ = false;
  bool response_logged_ = false;
};

class ServerLoggingInterceptorFactory final
    : public ServerInterceptorFactoryInterface {
 public:
  ServerLoggingInterceptorFactory(string service_name, LogLevel min_log_level)
      : service_name_(::std::move(service_name)),
        min_log_level_(min_log_level) {}

  Interceptor* CreateServerInterceptor(ServerRpcInfo* info) override {
    return new ServerLoggingInterceptor(service_name_, min_log_level_, info);
  }

 private:
  string service_name_;
  LogLevel min_log_level_;
};

}  // namespace

Logger::Logger(string_view service_name) : service_name_(service_name) {}

void Logger::SetMinLogLevel(LogLevel min_log_level) {
  min_log_level_ = min_log_level;
}

void Logger::SetMinLogLevel(string_view min_log_level) {
  SetMinLogLevel(ParseLogLevel(min_log_level));
}

void Logger::Debug(string_view event, initializer_list<LogField> fields) const {
  LogStructured(min_log_level_, "debug", service_name_, event, fields);
}

void Logger::Info(string_view event, initializer_list<LogField> fields) const {
  LogStructured(min_log_level_, "info", service_name_, event, fields);
}

void Logger::Warning(string_view event,
                     initializer_list<LogField> fields) const {
  LogStructured(min_log_level_, "warning", service_name_, event, fields);
}

void Logger::Error(string_view event, initializer_list<LogField> fields) const {
  LogStructured(min_log_level_, "error", service_name_, event, fields);
}

vector<unique_ptr<ServerInterceptorFactoryInterface>>
CreateServerLoggingInterceptorCreators(const Logger& logger) {
  vector<unique_ptr<ServerInterceptorFactoryInterface>> interceptor_creators;
  interceptor_creators.push_back(make_unique<ServerLoggingInterceptorFactory>(
      logger.service_name(), logger.min_log_level()));
  return interceptor_creators;
}

}  // namespace utilities
}  // namespace shooting_star
