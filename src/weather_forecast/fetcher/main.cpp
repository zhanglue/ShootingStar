#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <cstdlib>
#include <exception>
#include <memory>
#include <string>

#include "src/utilities/global_config/global_config.h"
#include "src/utilities/logger/logger.h"
#include "src/utilities/logger/logger_registry.h"
#include "src/weather_forecast/fetcher/fetcher_service.h"

namespace {

using ::grpc::EnableDefaultHealthCheckService;
using ::grpc::InsecureServerCredentials;
using ::grpc::Server;
using ::grpc::ServerBuilder;
using ::grpc::reflection::InitProtoReflectionServerBuilderPlugin;
using ::shooting_star::utilities::ConfigArguments;
using ::shooting_star::utilities::ConfigYAML;
using ::shooting_star::utilities::CreateServerLoggingInterceptorCreators;
using ::shooting_star::utilities::GlobalConfig;
using ::shooting_star::utilities::Logger;
using ::shooting_star::utilities::LoggerRegistry;
using ::std::make_shared;
using ::std::string;
using ::std::unique_ptr;
using ::weather_flow::FetcherServiceImpl;

constexpr const char* kServiceName = "fetcher";

}  // namespace

int main(int argc, char** argv) {
  try {
    const GlobalConfig& config = GlobalConfig::Initialize(kServiceName);
    ConfigYAML::ApplyStartupFile(argc, argv, argv[0]);
    ConfigArguments::Apply(argc, argv);
    LoggerRegistry::Register(make_shared<Logger>(kServiceName));
    LoggerRegistry::SetDefaultLoggerName(kServiceName);

    string server_address = config.GetListenAddress();
    FetcherServiceImpl service;

    EnableDefaultHealthCheckService(true);
    InitProtoReflectionServerBuilderPlugin();
    ServerBuilder builder;
    const Logger& logger = LoggerRegistry::Get();
    builder.experimental().SetInterceptorCreators(
        CreateServerLoggingInterceptorCreators(logger));
    // Listen on the given address without any authentication mechanism.
    builder.AddListeningPort(server_address, InsecureServerCredentials());
    // Register "service" as the instance through which we'll communicate with
    // clients. In this case it corresponds to an *synchronous* service.
    builder.RegisterService(&service);
    // Finally assemble the server.
    unique_ptr<Server> server(builder.BuildAndStart());
    logger.Info(
      "server_started",
      {
        {"listen_address", server_address},
      });

    // Wait for the server to shutdown. Note that some other thread must be
    // responsible for shutting down the server for this call to ever return.
    server->Wait();
  } catch (const ::std::exception& ex) {
    const Logger& logger = LoggerRegistry::Get();
    logger.Error("server_startup_failed", {{"error", ex.what()}, });
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
