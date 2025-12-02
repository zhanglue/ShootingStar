#include "src/recommender_engine/gateway/gateway_service.h"

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <iostream>
#include <memory>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_format.h"

#include "protos/recommender_engine.grpc.pb.h"

namespace recommender_engine {

using ::grpc::Server;
using ::grpc::ServerBuilder;
using ::grpc::ServerContext;
using ::grpc::Status;
using ::grpc::StatusCode;
using ::std::cout;
using ::std::endl;
using ::std::string;
using ::std::format;

// Hardcoded user profile for demo.
GatewayServiceImpl::GatewayServiceImpl() {
	// -------- Profile 1 --------
	{
		Profile p;
		p.set_user_id(1001);
		p.set_name("Alice");
		p.set_gender("Female");
		profiles_.emplace(p.user_id(), p);
	}

	// -------- Profile 2 --------
	{
		Profile p;
		p.set_user_id(1002);
		p.set_name("Bob");
		p.set_gender("Male");
		profiles_.emplace(p.user_id(), p);
	}

	// -------- Profile 3 --------
	{
		Profile p;
		p.set_user_id(1003);
		p.set_name("Charlie");
		p.set_gender("Male");
		profiles_.emplace(p.user_id(), p);
	}
}

Status GatewayServiceImpl::Recommend(
    ServerContext* context,
    const RecommendRequest* request,
    RecommendResponse* response) {

  int user_id = request->user_id();
  auto it = profiles_.find(user_id);
  if (it == profiles_.end()) {
    return Status(
        StatusCode::NOT_FOUND,
        format("User ID of {} not found.", request->user_id()));
  }

  response->mutable_profile()->CopyFrom(it->second);

  return Status::OK;
}

}  // namespace recommender_engine
