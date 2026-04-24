#include "src/utilities/logger/logger.h"
#include "src/utilities/logger/logger_registry.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace shooting_star {
namespace utilities {
namespace {

using ::std::string;
using ::std::vector;

TEST(LoggerTest, LogsDeferredVectorFieldsBackedByStableStorage) {
  LoggerRegistry::ClearForTest();
  LoggerRegistry::Register(::std::make_shared<Logger>("logger_test"));
  LoggerRegistry::SetDefaultLoggerName("logger_test");
  vector<string> values;
  values.push_back("first");
  values.push_back("<redacted>");
  values.push_back("last");

  vector<LogField> fields;
  fields.push_back({"alpha", values[0]});
  fields.push_back({"password", values[1]});
  fields.push_back({"omega", values[2]});

  ::testing::internal::CaptureStdout();
  const Logger& logger = LoggerRegistry::Get();
  logger.Info("resolved_config", fields);
  const string logs = ::testing::internal::GetCapturedStdout();

  EXPECT_NE(logs.find("\"logger_name\":\"logger_test\""), string::npos);
  EXPECT_NE(logs.find("\"event\":\"resolved_config\""), string::npos);
  EXPECT_NE(logs.find("\"alpha\":\"first\""), string::npos);
  EXPECT_NE(logs.find("\"password\":\"<redacted>\""), string::npos);
  EXPECT_NE(logs.find("\"omega\":\"last\""), string::npos);
}

TEST(LoggerRegistryTest, CreatesBlankLoggerWhenRegistryIsEmpty) {
  LoggerRegistry::ClearForTest();

  ::testing::internal::CaptureStdout();
  const Logger& logger = LoggerRegistry::Get();
  logger.Info("after_blank_logger_created");
  const string logs = ::testing::internal::GetCapturedStdout();

  EXPECT_EQ(logger.logger_name(), "blank_logger");
  EXPECT_NE(logs.find("\"event\":\"blank_logger_used\""), string::npos);
  EXPECT_NE(logs.find("\"event\":\"after_blank_logger_created\""),
            string::npos);
}

TEST(LoggerRegistryTest, ReturnsDefaultLoggerWhenRegistered) {
  LoggerRegistry::ClearForTest();
  LoggerRegistry::Register(::std::make_shared<Logger>("profile"));
  LoggerRegistry::SetDefaultLoggerName("profile");

  const Logger& logger = LoggerRegistry::Get();

  EXPECT_EQ(logger.logger_name(), "profile");
}

TEST(LoggerRegistryTest, RejectsDefaultLoggerNameWhenLoggerIsMissing) {
  LoggerRegistry::ClearForTest();

  ::testing::internal::CaptureStdout();
  LoggerRegistry::SetDefaultLoggerName("missing");
  const Logger& logger = LoggerRegistry::Get();
  const string logs = ::testing::internal::GetCapturedStdout();

  EXPECT_EQ(logger.logger_name(), "blank_logger");
  EXPECT_NE(logs.find("\"event\":\"default_logger_name_set_failed\""),
            string::npos);
  EXPECT_NE(logs.find("\"requested_logger_name\":\"missing\""), string::npos);
}

TEST(LoggerRegistryTest, KeepsFirstDefaultLoggerName) {
  LoggerRegistry::ClearForTest();
  LoggerRegistry::Register(::std::make_shared<Logger>("first"));
  LoggerRegistry::Register(::std::make_shared<Logger>("second"));
  LoggerRegistry::SetDefaultLoggerName("first");

  ::testing::internal::CaptureStdout();
  LoggerRegistry::SetDefaultLoggerName("second");
  const string logs = ::testing::internal::GetCapturedStdout();

  const Logger& logger = LoggerRegistry::Get();
  EXPECT_EQ(logger.logger_name(), "first");
  EXPECT_NE(logs.find("\"logger_name\":\"first\""), string::npos);
  EXPECT_NE(logs.find("\"event\":\"default_logger_name_set_ignored\""),
            string::npos);
}

TEST(LoggerRegistryTest, LogsWhenBlankLoggerIsRequestedAgain) {
  LoggerRegistry::ClearForTest();
  static_cast<void>(LoggerRegistry::Get());

  ::testing::internal::CaptureStdout();
  const Logger& logger = LoggerRegistry::Get("blank_logger");
  const string logs = ::testing::internal::GetCapturedStdout();

  EXPECT_EQ(logger.logger_name(), "blank_logger");
  EXPECT_NE(logs.find("\"event\":\"blank_logger_used\""), string::npos);
}

}  // namespace
}  // namespace utilities
}  // namespace shooting_star
