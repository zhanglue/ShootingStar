#include <memory>
#include <string>

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_format.h"

#include "src/recommendation_engine/retrieval/retrievers/item_cf/retriever_item_cf.h"
#include "src/utilities/grpc_logger/grpc_logger.h"

ABSL_FLAG(uint16_t, port, 50210, "Server port for the service");

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  const ::shooting_star::utilities::Logger logger("retriever_item_cf");

  const ::std::string server_address =
      absl::StrFormat("0.0.0.0:%d", absl::GetFlag(FLAGS_port));
  ::recommendation_engine::RetrieverItemCf service;

  ::grpc::EnableDefaultHealthCheckService(true);
  ::grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ::grpc::ServerBuilder builder;
  builder.experimental().SetInterceptorCreators(
      ::shooting_star::utilities::CreateServerLoggingInterceptorCreators(logger));
  builder.AddListeningPort(server_address, ::grpc::InsecureServerCredentials());
  builder.RegisterService(&service);

  ::std::unique_ptr<::grpc::Server> server(builder.BuildAndStart());
  logger.Info(
      "server_started",
      {
          {"listen_address", server_address},
      });
  server->Wait();

  return 0;
}
