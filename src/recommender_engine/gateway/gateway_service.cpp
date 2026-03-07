#include "src/recommender_engine/gateway/gateway_service.h"

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

namespace recommender_engine {

// Hardcoded user profile for demo.
GatewayServiceImpl::GatewayServiceImpl() {
	// -------- Profile 1 --------
	{
		Profile p;
		p.set_user_id(1001);
		profiles_.emplace(p.user_id(), p);
	}

	// -------- Profile 2 --------
	{
		Profile p;
		p.set_user_id(1002);
		profiles_.emplace(p.user_id(), p);
	}

	// -------- Profile 3 --------
	{
		Profile p;
		p.set_user_id(1003);
		profiles_.emplace(p.user_id(), p);
	}
}

::grpc::Status GatewayServiceImpl::Recommend(
    ::grpc::ServerContext* context,
    const RecommendRequest* request,
    RecommendResponse* response) {

  int user_id = request->user_id();
  auto it = profiles_.find(user_id);
  if (it == profiles_.end()) {
	return ::grpc::Status(
		 ::grpc::StatusCode::NOT_FOUND,
         ::std::format("User ID of {} not found.", request->user_id()));
  }

  response->mutable_profile()->CopyFrom(it->second);

  return ::grpc::Status::OK;
}

}  // namespace recommender_engine
