#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <memory>
#include <string>

#include "src/recommendation_engine/retrieval/retrievers/user_cf/retriever_user_cf.h"
#include "src/utilities/global_config/global_config.h"
#include "src/utilities/logger/logger.h"
#include "src/utilities/logger/logger_registry.h"

int main(int argc, char** argv) {
  const ::shooting_star::utilities::GlobalConfig& config =
      ::shooting_star::utilities::GlobalConfig::Get();
  ::shooting_star::utilities::ConfigYAML::ApplyStartupFile(argc, argv, argv[0]);
  ::shooting_star::utilities::ConfigArguments::Apply(argc, argv);
  ::shooting_star::utilities::LoggerRegistry::Register(
      ::std::make_shared<::shooting_star::utilities::Logger>(
          "retriever_user_cf"));
  ::shooting_star::utilities::LoggerRegistry::SetDefaultLoggerName(
      "retriever_user_cf");

  const ::std::string server_address = config.GetListenAddress();
  ::recommendation_engine::RetrieverUserCf service;

  ::grpc::EnableDefaultHealthCheckService(true);
  ::grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ::grpc::ServerBuilder builder;
  const ::shooting_star::utilities::Logger& logger =
      ::shooting_star::utilities::LoggerRegistry::Get();
  builder.experimental().SetInterceptorCreators(
      ::shooting_star::utilities::CreateServerLoggingInterceptorCreators(
          logger));
  builder.AddListeningPort(server_address, ::grpc::InsecureServerCredentials());
  builder.RegisterService(&service);

  ::std::unique_ptr<::grpc::Server> server(builder.BuildAndStart());
  logger.Info("server_started", {
                                    {"listen_address", server_address},
                                });
  server->Wait();

  return 0;
}
