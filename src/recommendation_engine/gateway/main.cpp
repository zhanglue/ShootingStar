#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <cstdlib>
#include <exception>
#include <memory>
#include <string>

#include "src/recommendation_engine/gateway/gateway_service.h"
#include "src/utilities/global_config/global_config.h"
#include "src/utilities/logger/logger.h"
#include "src/utilities/logger/logger_registry.h"

namespace {

using ::grpc::CreateChannel;
using ::grpc::EnableDefaultHealthCheckService;
using ::grpc::InsecureChannelCredentials;
using ::grpc::InsecureServerCredentials;
using ::grpc::Server;
using ::grpc::ServerBuilder;
using ::grpc::reflection::InitProtoReflectionServerBuilderPlugin;
using ::recommendation_engine::GatewayServiceImpl;
using ::shooting_star::utilities::ConfigArguments;
using ::shooting_star::utilities::ConfigYAML;
using ::shooting_star::utilities::CreateServerLoggingInterceptorCreators;
using ::shooting_star::utilities::GlobalConfig;
using ::shooting_star::utilities::Logger;
using ::shooting_star::utilities::LoggerRegistry;
using ::std::make_shared;
using ::std::string;
using ::std::unique_ptr;

constexpr const char* kServiceName = "gateway";

}  // namespace

int main(int argc, char** argv) {
  try {
    const GlobalConfig& config = GlobalConfig::Initialize(kServiceName);
    ConfigYAML::ApplyStartupFile(argc, argv, argv[0]);
    ConfigArguments::Apply(argc, argv);

    auto gateway_logger = make_shared<Logger>(kServiceName);
    gateway_logger->SetMinLogLevel(config.GetLogLevel());
    LoggerRegistry::Register(::std::move(gateway_logger));
    LoggerRegistry::SetDefaultLoggerName(kServiceName);

    const Logger& logger = LoggerRegistry::Get();
    logger.Info("config_loaded", {{"config_path", config.GetConfigPath()}, });
    config.LogResolvedConfig(logger);

    string server_address = config.GetListenAddress();
    const string profile_service_address = config.GetProfileServiceAddress();
    const string retrieval_service_address = config.GetRetrievalServiceAddress();
    const string ranking_service_address = config.GetRankingServiceAddress();
    GatewayServiceImpl service(
        CreateChannel(profile_service_address, InsecureChannelCredentials()),
        CreateChannel(retrieval_service_address, InsecureChannelCredentials()),
        CreateChannel(ranking_service_address, InsecureChannelCredentials()));

    EnableDefaultHealthCheckService(true);
    InitProtoReflectionServerBuilderPlugin();
    ServerBuilder builder;
    builder.experimental().SetInterceptorCreators(
        CreateServerLoggingInterceptorCreators(logger));
    builder.AddListeningPort(server_address, InsecureServerCredentials());
    builder.RegisterService(&service);

    unique_ptr<Server> server(builder.BuildAndStart());
    logger.Info(
      "server_started",
      {
        {"listen_address", server_address},
        {"profile_service_address", profile_service_address},
        {"retrieval_service_address", retrieval_service_address},
        {"ranking_service_address", ranking_service_address},
      });
    server->Wait();
  } catch (const ::std::exception& ex) {
    const Logger& logger = LoggerRegistry::Get();
    logger.Error("server_startup_failed", {{"error", ex.what()}, });
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
