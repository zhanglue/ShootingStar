#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "src/recommendation_engine/profile/profile_service.h"
#include "src/utilities/global_config/global_config.h"
#include "src/utilities/logger/logger.h"
#include "src/utilities/logger/logger_registry.h"

namespace {

using ::recommendation_engine::ProfileServiceImpl;
using ::shooting_star::utilities::ConfigArguments;
using ::shooting_star::utilities::ConfigYAML;
using ::shooting_star::utilities::GlobalConfig;
using ::shooting_star::utilities::LogField;
using ::shooting_star::utilities::Logger;
using ::shooting_star::utilities::LoggerRegistry;
using ::std::string;
using ::std::string_view;
using ::std::vector;

constexpr string_view kServiceName = "profile";
constexpr string_view kRedactedValue = "<redacted>";

bool IsSensitiveConfigKey(string_view key) {
  return key.find("password") != string_view::npos;
}

void LogResolvedConfig(const GlobalConfig& config) {
  const vector<::std::pair<string, string>> values = config.GetResolvedValues();
  vector<LogField> fields;
  fields.reserve(values.size());
  for (const auto& [key, value] : values) {
    fields.push_back(
        {key, IsSensitiveConfigKey(key) ? kRedactedValue : string_view(value)});
  }
  const Logger& logger = LoggerRegistry::Get();
  logger.Info("resolved_config", fields);
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const GlobalConfig& config = GlobalConfig::Get();
    ConfigYAML::ApplyStartupFile(argc, argv, argv[0]);
    ConfigArguments::Apply(argc, argv);

    auto profile_logger = ::std::make_shared<Logger>(kServiceName);
    profile_logger->SetMinLogLevel(config.GetLogLevel());
    LoggerRegistry::Register(::std::move(profile_logger));
    LoggerRegistry::SetDefaultLoggerName(kServiceName);

    const Logger& logger = LoggerRegistry::Get();
    logger.Info("config_loaded", {
                                     {"config_path", config.GetConfigPath()},
                                 });
    LogResolvedConfig(config);

    const string server_address = config.GetListenAddress();
    ProfileServiceImpl service(config);

    ::grpc::EnableDefaultHealthCheckService(true);
    ::grpc::reflection::InitProtoReflectionServerBuilderPlugin();
    ::grpc::ServerBuilder builder;
    builder.experimental().SetInterceptorCreators(
        ::shooting_star::utilities::CreateServerLoggingInterceptorCreators(
            logger));
    builder.AddListeningPort(server_address,
                             ::grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    ::std::unique_ptr<::grpc::Server> server(builder.BuildAndStart());
    logger.Info("server_started", {
                                      {"listen_address", server_address},
                                  });
    server->Wait();
  } catch (const ::std::exception& ex) {
    const Logger& logger = LoggerRegistry::Get();
    logger.Error("server_startup_failed", {
                                              {"error", ex.what()},
                                          });
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
