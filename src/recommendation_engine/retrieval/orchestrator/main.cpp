#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <memory>
#include <string>

#include "src/recommendation_engine/retrieval/orchestrator/retrieval_orchestrator.h"
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
using ::recommendation_engine::RetrievalOrchestrator;
using ::shooting_star::utilities::ConfigArguments;
using ::shooting_star::utilities::ConfigYAML;
using ::shooting_star::utilities::CreateServerLoggingInterceptorCreators;
using ::shooting_star::utilities::GlobalConfig;
using ::shooting_star::utilities::Logger;
using ::shooting_star::utilities::LoggerRegistry;
using ::std::make_shared;
using ::std::string;
using ::std::unique_ptr;

constexpr const char* kServiceName = "retrieval_orchestrator";

}  // namespace

int main(int argc, char** argv) {
  const GlobalConfig& config = GlobalConfig::Initialize(kServiceName);
  ConfigYAML::ApplyStartupFile(argc, argv, argv[0]);
  ConfigArguments::Apply(argc, argv);
  LoggerRegistry::Register(make_shared<Logger>(kServiceName));
  LoggerRegistry::SetDefaultLoggerName(kServiceName);

  const string server_address = config.GetListenAddress();
  const string item_cf_service_address =
      config.GetRetrieverItemCfAddress();
  const string user_cf_service_address =
      config.GetRetrieverUserCfAddress();
  RetrievalOrchestrator service(
      CreateChannel(item_cf_service_address, InsecureChannelCredentials()),
      CreateChannel(user_cf_service_address, InsecureChannelCredentials()));

  EnableDefaultHealthCheckService(true);
  InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;
  const Logger& logger = LoggerRegistry::Get();
  builder.experimental().SetInterceptorCreators(
      CreateServerLoggingInterceptorCreators(logger));
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);

  unique_ptr<Server> server(builder.BuildAndStart());
  logger.Info(
    "server_started",
    {
      {"listen_address", server_address},
      {"retriever_item_cf_address", item_cf_service_address},
      {"retriever_user_cf_address", user_cf_service_address},
    });
  server->Wait();

  return 0;
}
