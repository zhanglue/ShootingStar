#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <memory>
#include <string>

#include "src/utilities/global_config/global_config.h"
#include "src/utilities/logger/logger.h"
#include "src/utilities/logger/logger_registry.h"
#include "src/weather_forecast/fetcher/fetcher_service.h"

using grpc::Server;
using grpc::ServerBuilder;
using std::string;

int main(int argc, char** argv) {
  const ::shooting_star::utilities::GlobalConfig& config =
      ::shooting_star::utilities::GlobalConfig::Get();
  ::shooting_star::utilities::ConfigYAML::ApplyStartupFile(argc, argv, argv[0]);
  ::shooting_star::utilities::ConfigArguments::Apply(argc, argv);
  ::shooting_star::utilities::LoggerRegistry::Register(
      ::std::make_shared<::shooting_star::utilities::Logger>("fetcher"));
  ::shooting_star::utilities::LoggerRegistry::SetDefaultLoggerName("fetcher");

  string server_address = config.GetListenAddress();
  weather_flow::FetcherServiceImpl service;

  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;
  const ::shooting_star::utilities::Logger& logger =
      ::shooting_star::utilities::LoggerRegistry::Get();
  builder.experimental().SetInterceptorCreators(
      ::shooting_star::utilities::CreateServerLoggingInterceptorCreators(
          logger));
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&service);
  // Finally assemble the server.
  std::unique_ptr<Server> server(builder.BuildAndStart());
  logger.Info("server_started", {
                                    {"listen_address", server_address},
                                });

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();

  return 0;
}
