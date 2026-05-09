#pragma once

#include <memory>

#include <grpcpp/grpcpp.h>

#include "protos/recommendation_engine/profile.grpc.pb.h"
#include "protos/recommendation_engine/recommendation_engine.grpc.pb.h"
#include "protos/recommendation_engine/ranking.grpc.pb.h"
#include "protos/recommendation_engine/retrieval.grpc.pb.h"
#include "src/utilities/global_config/global_config.h"

namespace shooting_star::recommendation_engine {

class GatewayServiceImpl final : public Gateway::Service {
 public:
  GatewayServiceImpl(::std::unique_ptr<ProfileService::Stub> profile_stub,
                     ::std::unique_ptr<RetrievalService::Stub> retrieval_stub,
                     ::std::unique_ptr<RankingService::Stub> ranking_stub);

  static ::std::unique_ptr<GatewayServiceImpl> Create(
      const ::shooting_star::utilities::GlobalConfig& config);

  ::grpc::Status Recommend(::grpc::ServerContext* context,
                           const RecommendRequest* request,
                           RecommendResponse* response) override;

 private:
  ::grpc::Status FetchProfile(const RecommendRequest& request, Profile* profile,
                              RecommendationStatus* recommendation_status) const;
  ::grpc::Status FetchCandidates(const RecommendRequest& request,
                                 const Profile& profile,
                                 RetrieveResponse* retrieval_response,
                                 RecommendationStatus* recommendation_status) const;
  ::grpc::Status FetchRankedCandidates(const RecommendRequest& request,
                                       const Profile& profile,
                                       const RetrieveResponse& retrieval_response,
                                       RecommendResponse* response,
                                 RecommendationStatus* recommendation_status) const;

  ::std::unique_ptr<ProfileService::Stub> profile_stub_;
  ::std::unique_ptr<RetrievalService::Stub> retrieval_stub_;
  ::std::unique_ptr<RankingService::Stub> ranking_stub_;
};

}  // namespace shooting_star::recommendation_engine
