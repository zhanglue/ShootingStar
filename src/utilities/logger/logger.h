#ifndef SRC_UTILITIES_LOGGER_LOGGER_H_
#define SRC_UTILITIES_LOGGER_LOGGER_H_

#include <grpcpp/grpcpp.h>
#include <grpcpp/support/server_interceptor.h>

#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace shooting_star {
namespace utilities {

struct LogField {
  ::std::string_view key;
  ::std::string_view value;
};

enum class LogLevel {
  kDebug = 0,
  kInfo = 1,
  kWarning = 2,
  kError = 3,
};

class Logger {
 public:
  explicit Logger(::std::string_view service_name);

  void SetMinLogLevel(LogLevel min_log_level);
  void SetMinLogLevel(::std::string_view min_log_level);

  void Debug(::std::string_view event,
             ::std::initializer_list<LogField> fields = {}) const;
  void Debug(::std::string_view event,
             const ::std::vector<LogField>& fields) const;
  void Info(::std::string_view event,
            ::std::initializer_list<LogField> fields = {}) const;
  void Info(::std::string_view event,
            const ::std::vector<LogField>& fields) const;
  void Warning(::std::string_view event,
               ::std::initializer_list<LogField> fields = {}) const;
  void Warning(::std::string_view event,
               const ::std::vector<LogField>& fields) const;
  void Error(::std::string_view event,
             ::std::initializer_list<LogField> fields = {}) const;
  void Error(::std::string_view event,
             const ::std::vector<LogField>& fields) const;

  const ::std::string& service_name() const { return service_name_; }
  LogLevel min_log_level() const { return min_log_level_; }

 private:
  ::std::string service_name_;
  LogLevel min_log_level_ = LogLevel::kInfo;
};

::std::vector<
    ::std::unique_ptr<::grpc::experimental::ServerInterceptorFactoryInterface>>
CreateServerLoggingInterceptorCreators(const Logger& logger);

}  // namespace utilities
}  // namespace shooting_star

#endif  // SRC_UTILITIES_LOGGER_LOGGER_H_
