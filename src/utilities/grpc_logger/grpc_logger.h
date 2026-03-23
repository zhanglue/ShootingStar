#ifndef SRC_UTILITIES_GRPC_LOGGER_GRPC_LOGGER_H_
#define SRC_UTILITIES_GRPC_LOGGER_GRPC_LOGGER_H_

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

class Logger {
 public:
  explicit Logger(::std::string_view service_name);

  void Info(::std::string_view event,
            ::std::initializer_list<LogField> fields = {}) const;
  void Error(::std::string_view event,
             ::std::initializer_list<LogField> fields = {}) const;

  const ::std::string& service_name() const { return service_name_; }

 private:
  ::std::string service_name_;
};

::std::vector<::std::unique_ptr<::grpc::experimental::ServerInterceptorFactoryInterface>>
CreateServerLoggingInterceptorCreators(const Logger& logger);

}  // namespace utilities
}  // namespace shooting_star

#endif  // SRC_UTILITIES_GRPC_LOGGER_GRPC_LOGGER_H_
