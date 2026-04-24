#include <memory>
#include <string>

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_format.h"

#include "src/weather_forecast/fetcher/fetcher_service.h"
#include "src/utilities/logger/logger.h"
#include "src/utilities/logger/logger_registry.h"

ABSL_FLAG(uint16_t, port, 40000, "Server port for the service");

using grpc::Server;
using grpc::ServerBuilder;
using std::string;

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  ::shooting_star::utilities::LoggerRegistry::Register(
      ::std::make_shared<::shooting_star::utilities::Logger>("fetcher"));
  ::shooting_star::utilities::LoggerRegistry::SetDefaultLoggerName("fetcher");

  string server_address = absl::StrFormat("0.0.0.0:%d", absl::GetFlag(FLAGS_port));
  weather_flow::FetcherServiceImpl service;

  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;
  const ::shooting_star::utilities::Logger& logger =
      ::shooting_star::utilities::LoggerRegistry::Get();
  builder.experimental().SetInterceptorCreators(
      ::shooting_star::utilities::CreateServerLoggingInterceptorCreators(logger));
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&service);
  // Finally assemble the server.
  std::unique_ptr<Server> server(builder.BuildAndStart());
  logger.Info(
      "server_started",
      {
          {"listen_address", server_address},
      });

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();

  return 0;
}
