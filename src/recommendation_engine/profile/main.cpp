#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <cstdlib>
#include <exception>
#include <memory>
#include <string>

#include "src/recommendation_engine/profile/profile_service.h"
#include "src/utilities/global_config/global_config.h"
#include "src/utilities/logger/logger.h"
#include "src/utilities/logger/logger_registry.h"

namespace {

using ::grpc::EnableDefaultHealthCheckService;
using ::grpc::InsecureServerCredentials;
using ::grpc::Server;
using ::grpc::ServerBuilder;
using ::grpc::reflection::InitProtoReflectionServerBuilderPlugin;
using ::recommendation_engine::ProfileServiceImpl;
using ::shooting_star::utilities::ConfigArguments;
using ::shooting_star::utilities::ConfigYAML;
using ::shooting_star::utilities::CreateServerLoggingInterceptorCreators;
using ::shooting_star::utilities::GlobalConfig;
using ::shooting_star::utilities::Logger;
using ::shooting_star::utilities::LoggerRegistry;
using ::std::make_shared;
using ::std::string;
using ::std::unique_ptr;

constexpr const char* kServiceName = "profile";

}  // namespace

int main(int argc, char** argv) {
  try {
    const GlobalConfig& config = GlobalConfig::Initialize(kServiceName);
    ConfigYAML::ApplyStartupFile(argc, argv, argv[0]);
    ConfigArguments::Apply(argc, argv);

    auto profile_logger = make_shared<Logger>(kServiceName);
    profile_logger->SetMinLogLevel(config.GetLogLevel());
    LoggerRegistry::Register(::std::move(profile_logger));
    LoggerRegistry::SetDefaultLoggerName(kServiceName);

    const Logger& logger = LoggerRegistry::Get();
    logger.Info("config_loaded", {{"config_path", config.GetConfigPath()}, });
    config.LogResolvedConfig(logger);

    const string server_address = config.GetListenAddress();
    ProfileServiceImpl service(config);

    EnableDefaultHealthCheckService(true);
    InitProtoReflectionServerBuilderPlugin();
    ServerBuilder builder;
    builder.experimental().SetInterceptorCreators(
        CreateServerLoggingInterceptorCreators(logger));
    builder.AddListeningPort(server_address, InsecureServerCredentials());
    builder.RegisterService(&service);

    unique_ptr<Server> server(builder.BuildAndStart());
    logger.Info("server_started", {{"listen_address", server_address}});
    server->Wait();
  } catch (const ::std::exception& ex) {
    const Logger& logger = LoggerRegistry::Get();
    logger.Error(
      "server_startup_failed",
      {
        {"error", ex.what()},
      });
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
