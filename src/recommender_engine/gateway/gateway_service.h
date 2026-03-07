#pragma once

#include <unordered_map>

#include <grpcpp/grpcpp.h>

#include "protos/recommender_engine.grpc.pb.h"

namespace recommender_engine {

using UserIdProfileMap = ::std::unordered_map<int, Profile>;

class GatewayServiceImpl final : public Gateway::Service {
 public:
  GatewayServiceImpl();

  ::grpc::Status Recommend(::grpc::ServerContext* context,
                           const RecommendRequest* request,
                           RecommendResponse* response) override;

 private:
  UserIdProfileMap profiles_;
};

}  // namespace recommender_engine
