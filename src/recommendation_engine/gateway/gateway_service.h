#pragma once

#include <memory>

#include <grpcpp/grpcpp.h>

#include "protos/recommendation_engine/profile.grpc.pb.h"
#include "protos/recommendation_engine/recommendation_engine.grpc.pb.h"
#include "protos/recommendation_engine/retrieval.grpc.pb.h"

namespace recommendation_engine {

class GatewayServiceImpl final : public Gateway::Service {
 public:
  GatewayServiceImpl(::std::shared_ptr<::grpc::Channel> profile_channel,
                     ::std::shared_ptr<::grpc::Channel> retrieval_channel);

  ::grpc::Status Recommend(::grpc::ServerContext* context,
                           const RecommendRequest* request,
                           RecommendResponse* response) override;

 private:
  ::grpc::Status FetchProfile(const RecommendRequest& request, Profile* profile,
                              RecommendationStatus* recommendation_status) const;
  ::grpc::Status FetchCandidates(const RecommendRequest& request,
                                 const Profile& profile,
                                 RecommendResponse* response,
                                 RecommendationStatus* recommendation_status) const;

  ::std::unique_ptr<ProfileService::Stub> profile_stub_;
  ::std::unique_ptr<RetrievalService::Stub> retrieval_stub_;
};

}  // namespace recommendation_engine
