#include "src/recommendation_engine/ranking/ranking_service.h"

#include <gtest/gtest.h>

namespace recommendation_engine {
namespace {

RankRequest BuildValidRequest() {
  RankRequest request;
  request.set_trace_id("trace-1");
  request.set_request_id("request-1");
  request.set_user_id(42);
  request.mutable_profile()->set_user_id(42);
  request.set_max_results(1);
  request.mutable_options()->set_include_item_features(true);

  CandidateItem* candidate = request.add_candidates();
  candidate->set_item_id(1);
  candidate->set_retriever("item_cf");
  candidate->set_retrieve_score(1.0);
  return request;
}

TEST(RankingServiceTest, BuildsConfiguredDefaultHeuristicRanker) {
  auto server =
      RankingServiceImpl::Create(::shooting_star::utilities::GlobalConfig::Get());
  RankRequest request = BuildValidRequest();
  RankResponse response;

  const ::grpc::Status status = server->Rank(nullptr, &request, &response);

  ASSERT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(response.status(), RankingServiceStatus::RANKING_SUCCESS);
  EXPECT_EQ(response.input_candidate_count(), 1);
  ASSERT_EQ(response.ranked_candidates_size(), 1);
  EXPECT_EQ(response.ranked_candidates(0).ranker(), "heuristic_v1");
  EXPECT_EQ(response.ranked_candidates(0).item_features().title(),
            "Toy Story");
}

TEST(RankingServiceTest, AcceptsRequestedConfiguredRanker) {
  auto server =
      RankingServiceImpl::Create(::shooting_star::utilities::GlobalConfig::Get());

  RankRequest request = BuildValidRequest();
  request.mutable_options()->set_ranker("heuristic_v1");
  RankResponse response;

  const ::grpc::Status status = server->Rank(nullptr, &request, &response);

  ASSERT_TRUE(status.ok()) << status.error_message();
  ASSERT_EQ(response.ranked_candidates_size(), 1);
  EXPECT_EQ(response.ranked_candidates(0).ranker(), "heuristic_v1");
}

TEST(RankingServiceTest, RejectsUnknownRequestRanker) {
  auto server =
      RankingServiceImpl::Create(::shooting_star::utilities::GlobalConfig::Get());

  RankRequest request = BuildValidRequest();
  request.mutable_options()->set_ranker("missing_ranker");
  RankResponse response;

  const ::grpc::Status status = server->Rank(nullptr, &request, &response);

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), ::grpc::StatusCode::INVALID_ARGUMENT);
  EXPECT_EQ(response.status(),
            RankingServiceStatus::RANKING_INVALID_REQUEST);
}

TEST(RankingServiceTest, RejectsInvalidRequestBeforeRanking) {
  auto server =
      RankingServiceImpl::Create(::shooting_star::utilities::GlobalConfig::Get());

  RankRequest request = BuildValidRequest();
  request.set_user_id(0);
  RankResponse response;

  const ::grpc::Status status = server->Rank(nullptr, &request, &response);

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), ::grpc::StatusCode::INVALID_ARGUMENT);
  EXPECT_EQ(response.status(),
            RankingServiceStatus::RANKING_INVALID_REQUEST);
}

}  // namespace
}  // namespace recommendation_engine
