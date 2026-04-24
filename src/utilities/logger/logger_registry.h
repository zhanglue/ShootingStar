#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "src/utilities/logger/logger.h"

namespace shooting_star {
namespace utilities {

class LoggerRegistry {
 public:
  static constexpr ::std::string_view kBlankLoggerName = "blank_logger";

  static void Register(::std::shared_ptr<Logger> logger);
  static void SetDefaultLoggerName(::std::string_view logger_name);
  static const Logger& Get(::std::string_view logger_name = "");
  static void ClearForTest();

 private:
  static ::std::string default_logger_name;
};

}  // namespace utilities
}  // namespace shooting_star
