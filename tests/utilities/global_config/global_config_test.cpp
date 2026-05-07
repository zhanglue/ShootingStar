#include "src/utilities/global_config/global_config.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "src/utilities/logger/logger.h"

namespace shooting_star {
namespace utilities {

class GlobalConfigTestAccess {
 public:
  static void Reset() { GlobalConfig::MutableGet().ResetToDefaults(); }
};

namespace {

using ::std::ofstream;
using ::std::string;
using ::std::filesystem::path;

path TestFilePath(const string& name) {
  const char* test_tmpdir = ::std::getenv("TEST_TMPDIR");
  if (test_tmpdir == nullptr) {
    return ::std::filesystem::current_path() / name;
  }
  return path(test_tmpdir) / name;
}

void WriteFile(const path& file_path, const string& content) {
  ::std::filesystem::create_directories(file_path.parent_path());
  ofstream fout(file_path);
  ASSERT_TRUE(fout.is_open());
  fout << content;
}

TEST(GlobalConfigTest, InitializesDefaultsOnFirstGet) {
  GlobalConfigTestAccess::Reset();

  const GlobalConfig& config = GlobalConfig::Get();

  EXPECT_EQ(config.GetServerPort(), 50000);
  EXPECT_EQ(config.GetProfileStoreType(), "local");
  EXPECT_EQ(config.GetItemIndexStoreType(), "local");
  EXPECT_EQ(config.GetItemIndexStoreDataPath(),
            "tests/testdata/recommendation_engine/"
            "local_recommendation_fixture/item_index.jsonl");
  EXPECT_EQ(config.GetRankingRankers(),
            (::std::vector<string>{"heuristic_v1"}));
  EXPECT_EQ(config.GetRankingDefaultRanker(), "heuristic_v1");
  EXPECT_EQ(config.GetLocalCacheCapacity(), 0);
  EXPECT_EQ(config.GetProfileServiceAddress(), "localhost:50100");
  EXPECT_EQ(config.GetRankingServiceAddress(), "localhost:50300");
  EXPECT_EQ(config.GetListenAddress(), "127.0.0.1:50000");
  EXPECT_EQ(config.GetRedisHost(), "localhost");
  EXPECT_EQ(config.GetRedisPort(), 6379);
  EXPECT_EQ(config.GetRedisKeyPrefix(), "rec:item_cf:v1:neighbors");
  EXPECT_EQ(config.GetRetrieverMaxTriggerSeedCount(), 24);
  EXPECT_EQ(config.GetUserCfTriggerSeedUserCount(), 10);
  EXPECT_DOUBLE_EQ(config.GetRetrieverItemCfScoreMultiplier(), 1.0);
  EXPECT_DOUBLE_EQ(config.GetRetrieverUserCfScoreMultiplier(), 1.0);
  EXPECT_EQ(config.GetRedisCommandBatchSize(), 8);
  EXPECT_DOUBLE_EQ(config.GetRetrievalRecallCandidateExpandRatio(), 1.0);
}

TEST(GlobalConfigTest, InitializeSetsServiceName) {
  GlobalConfigTestAccess::Reset();

  const GlobalConfig& config = GlobalConfig::Initialize("profile");

  EXPECT_EQ(config.GetServiceName(), "profile");
}

TEST(GlobalConfigTest, InitializeRejectsDifferentServiceNameAfterFirstSet) {
  GlobalConfigTestAccess::Reset();

  (void)GlobalConfig::Initialize("profile");
  EXPECT_THROW((void)GlobalConfig::Initialize("gateway"),
               ::std::invalid_argument);
}

TEST(GlobalConfigTest, AppliesYamlOverDefaultsOnlyForConfiguredFields) {
  GlobalConfigTestAccess::Reset();
  const path config_path = TestFilePath("profile_config.yaml");
  WriteFile(config_path,
            "server:\n"
            "  port: 51111\n"
            "retrieval:\n"
            "  recall_candidate_expand_ratio: 1.25\n"
            "elasticsearch:\n"
            "  http_client:\n"
            "    verify_ssl: false\n");

  ConfigYAML::ApplyFile(config_path.string());
  const GlobalConfig& config = GlobalConfig::Get();

  EXPECT_EQ(config.GetServerPort(), 51111);
  EXPECT_DOUBLE_EQ(config.GetRetrievalRecallCandidateExpandRatio(), 1.25);
  EXPECT_FALSE(config.GetElasticsearchHttpClientVerifySsl());
  EXPECT_EQ(config.GetProfileStoreType(), "local");
  EXPECT_EQ(config.GetElasticsearchUsername(), "elastic");
}

TEST(GlobalConfigTest, AppliesRankingRankerArrayFromYaml) {
  GlobalConfigTestAccess::Reset();
  const path config_path = TestFilePath("ranking_config.yaml");
  WriteFile(config_path,
            "ranking:\n"
            "  rankers:\n"
            "    - heuristic_v1\n"
            "  default_ranker: heuristic_v1\n"
            "item_index_store:\n"
            "  type: local\n"
            "  data_path: tests/testdata/recommendation_engine/"
            "local_recommendation_fixture/item_index.jsonl\n");

  ConfigYAML::ApplyFile(config_path.string());
  const GlobalConfig& config = GlobalConfig::Get();

  EXPECT_EQ(config.GetRankingRankers(),
            (::std::vector<string>{"heuristic_v1"}));
  EXPECT_EQ(config.GetRankingDefaultRanker(), "heuristic_v1");
  EXPECT_EQ(config.GetItemIndexStoreType(), "local");
}

TEST(GlobalConfigTest, AppliesOnlyExplicitCommandLineOverrides) {
  GlobalConfigTestAccess::Reset();
  const path config_path = TestFilePath("argument_override_config.yaml");
  WriteFile(config_path,
            "server:\n"
            "  port: 50123\n"
            "  log_level: WARNING\n");
  ConfigYAML::ApplyFile(config_path.string());

  char arg0[] = "profile";
  char arg1[] = "--port=50124";
  char arg2[] = "--es_http_client_verify_ssl=false";
  char arg3[] = "--user_cf_trigger_seed_user_count=17";
  char arg4[] = "--item_cf_score_multiplier=0.75";
  char arg5[] = "--user_cf_score_multiplier=7.5";
  char* argv[] = {arg0, arg1, arg2, arg3, arg4, arg5};
  ConfigArguments::Apply(6, argv);

  const GlobalConfig& config = GlobalConfig::Get();
  EXPECT_EQ(config.GetServerPort(), 50124);
  EXPECT_EQ(config.GetLogLevel(), "WARNING");
  EXPECT_FALSE(config.GetElasticsearchHttpClientVerifySsl());
  EXPECT_EQ(config.GetUserCfTriggerSeedUserCount(), 17);
  EXPECT_DOUBLE_EQ(config.GetRetrieverItemCfScoreMultiplier(), 0.75);
  EXPECT_DOUBLE_EQ(config.GetRetrieverUserCfScoreMultiplier(), 7.5);
}

TEST(GlobalConfigTest, SupportsGlobalLegacyArguments) {
  GlobalConfigTestAccess::Reset();

  char arg0[] = "gateway";
  char arg1[] = "--profile_service_host=profile.svc";
  char arg2[] = "--retrieval_service_port";
  char arg3[] = "60000";
  char arg4[] = "--ranking_service_host=ranking.svc";
  char* argv[] = {arg0, arg1, arg2, arg3, arg4};
  ConfigArguments::Apply(5, argv);

  const GlobalConfig& config = GlobalConfig::Get();
  EXPECT_EQ(config.GetProfileServiceAddress(), "profile.svc:50100");
  EXPECT_EQ(config.GetRetrievalServiceAddress(), "localhost:60000");
  EXPECT_EQ(config.GetRankingServiceAddress(), "ranking.svc:50300");
}

TEST(GlobalConfigTest, StartupFileUsesConfigPathArgument) {
  GlobalConfigTestAccess::Reset();
  const path config_path = TestFilePath("startup_profile_config.yaml");
  WriteFile(config_path,
            "server:\n"
            "  port: 50155\n");

  char arg0[] = "profile";
  string config_arg = "--config_path=" + config_path.string();
  char* argv[] = {arg0, config_arg.data()};
  ConfigYAML::ApplyStartupFile(2, argv, arg0);

  const GlobalConfig& config = GlobalConfig::Get();
  EXPECT_EQ(config.GetConfigPath(), config_path.string());
  EXPECT_EQ(config.GetServerPort(), 50155);
}

TEST(GlobalConfigTest, RejectsUnknownYamlKeys) {
  GlobalConfigTestAccess::Reset();
  const path config_path = TestFilePath("unknown_key_config.yaml");
  WriteFile(config_path,
            "server:\n"
            "  unknown_port: 50100\n");

  EXPECT_THROW(ConfigYAML::ApplyFile(config_path.string()),
               ::std::invalid_argument);
}

TEST(GlobalConfigTest, RejectsInvalidCommandLineValues) {
  GlobalConfigTestAccess::Reset();

  char arg0[] = "gateway";
  char arg1[] = "--port=70000";
  char* argv[] = {arg0, arg1};

  EXPECT_THROW(ConfigArguments::Apply(2, argv), ::std::out_of_range);
}

TEST(GlobalConfigTest, LogsOnlyRequestedConfigSection) {
  GlobalConfigTestAccess::Reset();
  const GlobalConfig& config = GlobalConfig::Get();
  const Logger logger("global_config_test");

  ::testing::internal::CaptureStdout();
  config.LogResolvedConfigSection(logger, "local_cache");
  const string logs = ::testing::internal::GetCapturedStdout();

  EXPECT_NE(logs.find("\"event\":\"resolved_config_section\""), string::npos);
  EXPECT_NE(logs.find("\"config_section\":\"local_cache\""), string::npos);
  EXPECT_NE(logs.find("\"local_cache.capacity\":\"0\""), string::npos);
  EXPECT_NE(logs.find("\"local_cache.ttl_seconds\":\"300\""), string::npos);
  EXPECT_EQ(logs.find("\"elasticsearch.base_url\":"), string::npos);
  EXPECT_EQ(logs.find("\"server.host\":"), string::npos);
}

TEST(GlobalConfigTest, AppliesRedisConfigSection) {
  GlobalConfigTestAccess::Reset();
  const path config_path = TestFilePath("redis_config.yaml");
  WriteFile(config_path,
            "redis:\n"
            "  host: redis-write.svc\n"
            "  port: 6380\n"
            "  db: 2\n"
            "  username: recommender\n"
            "  password: redis_secret\n"
            "  password_env: REDIS_PASSWORD\n"
            "  key_prefix: rec:item_cf:test:neighbors\n"
            "  connect_timeout_ms: 200\n"
            "  socket_timeout_ms: 300\n"
            "  pool_size: 8\n"
            "  pool_wait_timeout_ms: 40\n"
            "  retry:\n"
            "    max_attempts: 4\n"
            "    delay_ms: 5\n"
            "  command_batch_size: 16\n"
            "retriever:\n"
            "  max_trigger_seed_count: 32\n"
            "retriever_item_cf:\n"
            "  score_multiplier: 0.8\n"
            "retriever_user_cf:\n"
            "  trigger_seed_user_count: 12\n"
            "  score_multiplier: 8.0\n");

  ConfigYAML::ApplyFile(config_path.string());
  const GlobalConfig& config = GlobalConfig::Get();

  EXPECT_EQ(config.GetRedisHost(), "redis-write.svc");
  EXPECT_EQ(config.GetRedisPort(), 6380);
  EXPECT_EQ(config.GetRedisDb(), 2);
  EXPECT_EQ(config.GetRedisUsername(), "recommender");
  EXPECT_EQ(config.GetRedisPassword(), "redis_secret");
  EXPECT_EQ(config.GetRedisPasswordEnv(), "REDIS_PASSWORD");
  EXPECT_EQ(config.GetRedisKeyPrefix(), "rec:item_cf:test:neighbors");
  EXPECT_EQ(config.GetRedisConnectTimeoutMs(), 200);
  EXPECT_EQ(config.GetRedisSocketTimeoutMs(), 300);
  EXPECT_EQ(config.GetRedisPoolSize(), 8);
  EXPECT_EQ(config.GetRedisPoolWaitTimeoutMs(), 40);
  EXPECT_EQ(config.GetRedisRetryMaxAttempts(), 4);
  EXPECT_EQ(config.GetRedisRetryDelayMs(), 5);
  EXPECT_EQ(config.GetRedisCommandBatchSize(), 16);
  EXPECT_EQ(config.GetRetrieverMaxTriggerSeedCount(), 32);
  EXPECT_EQ(config.GetUserCfTriggerSeedUserCount(), 12);
  EXPECT_DOUBLE_EQ(config.GetRetrieverItemCfScoreMultiplier(), 0.8);
  EXPECT_DOUBLE_EQ(config.GetRetrieverUserCfScoreMultiplier(), 8.0);

}

}  // namespace
}  // namespace utilities
}  // namespace shooting_star
