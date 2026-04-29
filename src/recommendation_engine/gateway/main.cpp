#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

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
  const GlobalConfig& config = GlobalConfig::Initialize(kServiceName);
  ConfigYAML::ApplyStartupFile(argc, argv, argv[0]);
  ConfigArguments::Apply(argc, argv);
  LoggerRegistry::Register(make_shared<Logger>(kServiceName));
  LoggerRegistry::SetDefaultLoggerName(kServiceName);

  string server_address = config.GetListenAddress();
  const string profile_service_address = config.GetProfileServiceAddress();
  const string retrieval_service_address = config.GetRetrievalServiceAddress();
  GatewayServiceImpl service(
      CreateChannel(profile_service_address, InsecureChannelCredentials()),
      CreateChannel(retrieval_service_address, InsecureChannelCredentials()));

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
      {"profile_service_address", profile_service_address},
      {"retrieval_service_address", retrieval_service_address},
    });

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();

  return 0;
}
