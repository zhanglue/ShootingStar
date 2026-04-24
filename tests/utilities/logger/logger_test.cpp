#include "src/utilities/logger/logger.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace shooting_star {
namespace utilities {
namespace {

using ::std::string;
using ::std::vector;

TEST(LoggerTest, LogsDeferredVectorFieldsBackedByStableStorage) {
  const Logger logger("logger_test");
  vector<string> values;
  values.push_back("first");
  values.push_back("<redacted>");
  values.push_back("last");

  vector<LogField> fields;
  fields.push_back({"alpha", values[0]});
  fields.push_back({"password", values[1]});
  fields.push_back({"omega", values[2]});

  ::testing::internal::CaptureStdout();
  logger.Info("resolved_config", fields);
  const string logs = ::testing::internal::GetCapturedStdout();

  EXPECT_NE(logs.find("\"event\":\"resolved_config\""), string::npos);
  EXPECT_NE(logs.find("\"alpha\":\"first\""), string::npos);
  EXPECT_NE(logs.find("\"password\":\"<redacted>\""), string::npos);
  EXPECT_NE(logs.find("\"omega\":\"last\""), string::npos);
}

}  // namespace
}  // namespace utilities
}  // namespace shooting_star
