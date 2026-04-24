#include <memory>
#include <string>

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_format.h"

#include "src/recommendation_engine/retrieval/orchestrator/retrieval_orchestrator.h"
#include "src/utilities/logger/logger.h"
#include "src/utilities/logger/logger_registry.h"

ABSL_FLAG(uint16_t, port, 50200, "Server port for the service");
ABSL_FLAG(::std::string, retriever_item_cf_host, "localhost", "Item CF retriever host");
ABSL_FLAG(uint16_t, retriever_item_cf_port, 50210, "Item CF retriever port");
ABSL_FLAG(::std::string, retriever_user_cf_host, "localhost", "User CF retriever host");
ABSL_FLAG(uint16_t, retriever_user_cf_port, 50211, "User CF retriever port");

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  ::shooting_star::utilities::LoggerRegistry::Register(
      ::std::make_shared<::shooting_star::utilities::Logger>(
          "retrieval_orchestrator"));
  ::shooting_star::utilities::LoggerRegistry::SetDefaultLoggerName(
      "retrieval_orchestrator");

  const ::std::string server_address =
      absl::StrFormat("0.0.0.0:%d", absl::GetFlag(FLAGS_port));
  const ::std::string item_cf_service_address =
      absl::StrFormat("%s:%d", absl::GetFlag(FLAGS_retriever_item_cf_host),
                      absl::GetFlag(FLAGS_retriever_item_cf_port));
  const ::std::string user_cf_service_address =
      absl::StrFormat("%s:%d", absl::GetFlag(FLAGS_retriever_user_cf_host),
                      absl::GetFlag(FLAGS_retriever_user_cf_port));
  recommendation_engine::RetrievalOrchestrator service(
      grpc::CreateChannel(item_cf_service_address, grpc::InsecureChannelCredentials()),
      grpc::CreateChannel(user_cf_service_address, grpc::InsecureChannelCredentials()));

  ::grpc::EnableDefaultHealthCheckService(true);
  ::grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ::grpc::ServerBuilder builder;
  const ::shooting_star::utilities::Logger& logger =
      ::shooting_star::utilities::LoggerRegistry::Get();
  builder.experimental().SetInterceptorCreators(
      ::shooting_star::utilities::CreateServerLoggingInterceptorCreators(logger));
  builder.AddListeningPort(server_address, ::grpc::InsecureServerCredentials());
  builder.RegisterService(&service);

  ::std::unique_ptr<::grpc::Server> server(builder.BuildAndStart());
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
