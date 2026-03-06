#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "google/protobuf/struct.pb.h"
#include "google/protobuf/util/json_util.h"
#include "gtest/gtest.h"
#include "protos/profile.pb.h"
#include "src/utilities/pb_data_handler/pb_data_handler.h"

namespace {

using ::recommender_engine::Profile;
using ::shooting_star::utilities::PBDataHandler;
namespace fs = ::std::filesystem;
using ::google::protobuf::Struct;
using ::google::protobuf::Value;

class PBDataHandlerTest : public ::testing::Test {
 protected:
  fs::path RunfilePath(const std::string& relative_path) const {
    const char* test_srcdir = std::getenv("TEST_SRCDIR");
    const char* test_workspace = std::getenv("TEST_WORKSPACE");
    if (test_srcdir != nullptr && test_workspace != nullptr) {
      return fs::path(test_srcdir) / test_workspace / relative_path;
    }
    return fs::path(relative_path);
  }

  std::string ReadFileOrFail(const fs::path& path) const {
    std::ifstream fin(path);
    EXPECT_TRUE(fin.is_open()) << "Failed to open: " << path;
    if (!fin.is_open()) {
      return "";
    }
    return std::string(
        (std::istreambuf_iterator<char>(fin)),
        std::istreambuf_iterator<char>());
  }

  Struct LoadStructFromJsonFileOrFail(const fs::path& path) const {
    Struct root;
    const std::string json_content = ReadFileOrFail(path);
    if (json_content.empty()) {
      return root;
    }
    const auto status = ::google::protobuf::util::JsonStringToMessage(json_content, &root);
    EXPECT_TRUE(status.ok()) << status.ToString();
    return root;
  }
};

TEST_F(PBDataHandlerTest, JsonToPBDataDrivenFromJsonFile) {
  const fs::path cases_path = RunfilePath("tests/testdata/utilities/pb_data_handler/json_to_pb_cases.json");
  const Struct root = LoadStructFromJsonFileOrFail(cases_path);
  const auto cases_it = root.fields().find("cases");
  ASSERT_NE(cases_it, root.fields().end());
  ASSERT_TRUE(cases_it->second.has_list_value());

  for (const Value& test_case : cases_it->second.list_value().values()) {
    ASSERT_TRUE(test_case.has_struct_value());
    const auto& case_fields = test_case.struct_value().fields();

    const auto name_it = case_fields.find("name");
    ASSERT_NE(name_it, case_fields.end());
    SCOPED_TRACE(name_it->second.string_value());

    const auto expect_success_it = case_fields.find("expect_success");
    ASSERT_NE(expect_success_it, case_fields.end());
    const bool expect_success = expect_success_it->second.bool_value();

    std::string input_json;
    const auto input_it = case_fields.find("input");
    if (input_it != case_fields.end()) {
      ASSERT_TRUE(input_it->second.has_struct_value());
      auto status = ::google::protobuf::util::MessageToJsonString(input_it->second, &input_json);
      ASSERT_TRUE(status.ok()) << status.ToString();
    } else {
      const auto raw_input_it = case_fields.find("raw_input");
      ASSERT_NE(raw_input_it, case_fields.end());
      input_json = raw_input_it->second.string_value();
    }

    Profile profile;
    std::string error;
    const bool ok = PBDataHandler::JsonToPB(input_json, &profile, &error);
    EXPECT_EQ(ok, expect_success);

    if (expect_success) {
      const auto expected_it = case_fields.find("expected");
      ASSERT_NE(expected_it, case_fields.end());
      ASSERT_TRUE(expected_it->second.has_struct_value());
      const auto& expected_fields = expected_it->second.struct_value().fields();

      EXPECT_EQ(profile.user_id(), static_cast<int64_t>(expected_fields.at("user_id").number_value()));
      EXPECT_EQ(
          profile.demographics().birth_year(),
          static_cast<int32_t>(expected_fields.at("birth_year").number_value()));
      EXPECT_EQ(
          profile.social().following_size(),
          static_cast<int>(expected_fields.at("following_size").number_value()));
      EXPECT_EQ(
          profile.interests().tag_ids_size(),
          static_cast<int>(expected_fields.at("tag_ids_size").number_value()));
      EXPECT_EQ(
          profile.behaviors().active_timeframe(),
          static_cast<int64_t>(expected_fields.at("active_timeframe").number_value()));
      EXPECT_EQ(
          profile.session().last_login_time(),
          static_cast<int64_t>(expected_fields.at("last_login_time").number_value()));
    } else {
      const auto error_contains_it = case_fields.find("error_contains");
      ASSERT_NE(error_contains_it, case_fields.end());
      EXPECT_NE(error.find(error_contains_it->second.string_value()), std::string::npos);
    }
  }
}

TEST_F(PBDataHandlerTest, JsonToPBFailsForNullMessage) {
  std::string error;
  ASSERT_FALSE(PBDataHandler::JsonToPB("{}", nullptr, &error));
  EXPECT_NE(error.find("message is nullptr"), std::string::npos);
}

TEST_F(PBDataHandlerTest, PBToJsonWritesProtoFieldNames) {
  const fs::path input_file = RunfilePath("tests/testdata/utilities/pb_data_handler/roundtrip_profile.json");
  Profile profile;
  std::string error;
  ASSERT_TRUE(PBDataHandler::JsonFileToPB(input_file.string(), &profile, &error)) << error;

  std::string json;
  ASSERT_TRUE(PBDataHandler::PBToJson(profile, &json, &error)) << error;
  EXPECT_NE(json.find("\"user_id\""), std::string::npos);
  EXPECT_NE(json.find("\"location_id\""), std::string::npos);
}

TEST_F(PBDataHandlerTest, PBToJsonFailsForNullOutput) {
  Profile profile;
  std::string error;
  ASSERT_FALSE(PBDataHandler::PBToJson(profile, nullptr, &error));
  EXPECT_NE(error.find("json_out is nullptr"), std::string::npos);
}

TEST_F(PBDataHandlerTest, JsonFileToPBAndPBToJsonFileRoundTrip) {
  const fs::path json_input = RunfilePath("tests/testdata/utilities/pb_data_handler/roundtrip_profile.json");
  const fs::path temp_dir = fs::temp_directory_path();
  const fs::path json_output = temp_dir / "pb_data_handler_output_profile.json";

  Profile from_file;
  std::string error;
  ASSERT_TRUE(PBDataHandler::JsonFileToPB(json_input.string(), &from_file, &error)) << error;
  EXPECT_EQ(from_file.user_id(), 3001);
  EXPECT_EQ(from_file.session().recent_viewed_items_size(), 2);

  ASSERT_TRUE(PBDataHandler::PBToJsonFile(from_file, json_output.string(), &error)) << error;

  Profile roundtrip;
  ASSERT_TRUE(PBDataHandler::JsonFileToPB(json_output.string(), &roundtrip, &error)) << error;
  EXPECT_EQ(roundtrip.user_id(), 3001);
  EXPECT_EQ(roundtrip.demographics().location_id(), 9);

  std::error_code ec;
  fs::remove(json_output, ec);
}

TEST_F(PBDataHandlerTest, JsonFileToPBFailsForMissingFile) {
  Profile profile;
  std::string error;
  ASSERT_FALSE(PBDataHandler::JsonFileToPB(
      "/tmp/this_file_should_not_exist_1234567890.json",
      &profile,
      &error));
  EXPECT_NE(error.find("cannot open file"), std::string::npos);
}

}  // namespace
