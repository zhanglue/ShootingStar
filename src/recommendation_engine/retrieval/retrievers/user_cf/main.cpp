#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_format.h"

#include "src/recommendation_engine/retrieval/retrievers/user_cf/retriever_user_cf.h"

ABSL_FLAG(uint16_t, port, 50211, "Server port for the service");

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);

  const ::std::string server_address =
      absl::StrFormat("0.0.0.0:%d", absl::GetFlag(FLAGS_port));
  ::recommendation_engine::RetrieverUserCf service;

  ::grpc::EnableDefaultHealthCheckService(true);
  ::grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ::grpc::ServerBuilder builder;
  builder.AddListeningPort(server_address, ::grpc::InsecureServerCredentials());
  builder.RegisterService(&service);

  ::std::unique_ptr<::grpc::Server> server(builder.BuildAndStart());
  ::std::cout << "Server listening on " << server_address << ::std::endl;
  server->Wait();

  return 0;
}
