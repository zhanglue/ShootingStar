#pragma once

#include <memory>

#include <grpcpp/grpcpp.h>

#include "protos/profile.grpc.pb.h"
#include "protos/recommender_engine.grpc.pb.h"

namespace recommender_engine {

class GatewayServiceImpl final : public Gateway::Service {
 public:
  explicit GatewayServiceImpl(::std::shared_ptr<::grpc::Channel> profile_channel);

  ::grpc::Status Recommend(::grpc::ServerContext* context,
                           const RecommendRequest* request,
                           RecommendResponse* response) override;

 private:
  ::grpc::Status FetchProfile(const RecommendRequest& request, Profile* profile,
                              RecommenderStatus* recommender_status) const;

  ::std::unique_ptr<ProfileService::Stub> profile_stub_;
};

}  // namespace recommender_engine
