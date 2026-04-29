#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <memory>
#include <string>

#include "src/recommendation_engine/retrieval/orchestrator/retrieval_orchestrator.h"
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
          "retrieval_orchestrator"));
  ::shooting_star::utilities::LoggerRegistry::SetDefaultLoggerName(
      "retrieval_orchestrator");

  const ::std::string server_address = config.GetListenAddress();
  const ::std::string item_cf_service_address =
      config.GetRetrieverItemCfAddress();
  const ::std::string user_cf_service_address =
      config.GetRetrieverUserCfAddress();
  recommendation_engine::RetrievalOrchestrator service(
      grpc::CreateChannel(item_cf_service_address,
                          grpc::InsecureChannelCredentials()),
      grpc::CreateChannel(user_cf_service_address,
                          grpc::InsecureChannelCredentials()));

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
  logger.Info("server_started",
              {
                  {"listen_address", server_address},
                  {"retriever_item_cf_address", item_cf_service_address},
                  {"retriever_user_cf_address", user_cf_service_address},
              });
  server->Wait();

  return 0;
}
