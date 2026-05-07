#include "src/recommendation_engine/ranking/heuristic_ranker.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace recommendation_engine {
namespace {

using ::std::make_shared;
using ::std::map;
using ::std::optional;
using ::std::shared_ptr;
using ::std::string;
using ::std::vector;

class FakeItemIndexStore final : public ItemIndexStore {
 public:
  optional<ItemIndexEntry> FindByItemId(uint64_t item_id) const override {
    requests.push_back(item_id);
    const auto iter = entries.find(item_id);
    if (iter == entries.end()) {
      return ::std::nullopt;
    }
    return iter->second;
  }

  map<uint64_t, ItemIndexEntry> entries;
  mutable vector<uint64_t> requests;
};

ItemIndexEntry BuildItem(uint64_t item_id,
                         string title,
                         vector<string> genres,
                         double avg_rating,
                         int rating_count) {
  ItemIndexEntry item;
  item.item_id = item_id;
  item.title = ::std::move(title);
  item.year = 1995;
  item.genres = ::std::move(genres);
  item.rating.avg = avg_rating;
  item.rating.count = rating_count;
  return item;
}

CandidateItem BuildCandidate(uint64_t item_id,
                             string retriever,
                             double retrieve_score) {
  CandidateItem candidate;
  candidate.set_item_id(item_id);
  candidate.set_item_type(ItemType::ITEM_TYPE_VIDEO);
  candidate.set_retriever(::std::move(retriever));
  candidate.set_retrieve_score(retrieve_score);
  return candidate;
}

RankRequest BuildRequest() {
  RankRequest request;
  request.set_trace_id("trace-1");
  request.set_request_id("request-1");
  request.set_user_id(42);
  request.mutable_profile()->set_user_id(42);
  request.set_max_results(2);
  request.mutable_options()->set_include_score_factors(true);
  request.mutable_options()->set_include_item_features(true);

  GenreInterest* genre = request.mutable_profile()
                             ->mutable_interests()
                             ->add_genres();
  genre->set_genre("Adventure");
  genre->set_weight(1.0);

  GenreInterest* negative_genre = request.mutable_profile()
                                      ->mutable_negative_feedbacks()
                                      ->add_genres();
  negative_genre->set_genre("Drama");
  negative_genre->set_weight(2.0);

  request.add_candidates()->CopyFrom(
      BuildCandidate(1, "item_cf", 0.5));
  request.add_candidates()->CopyFrom(
      BuildCandidate(2, "user_cf", 1.0));
  request.add_candidates()->CopyFrom(
      BuildCandidate(1, "user_cf", 0.2));
  return request;
}

TEST(HeuristicRankerTest, RanksCandidatesWithItemFeaturesAndScoreFactors) {
  shared_ptr<FakeItemIndexStore> store = make_shared<FakeItemIndexStore>();
  store->entries.emplace(
      1, BuildItem(1, "Toy Story", {"Adventure", "Comedy"}, 4.0, 100));
  store->entries.emplace(
      2, BuildItem(2, "Drama Movie", {"Drama"}, 5.0, 100));

  HeuristicRanker ranker(store);
  RankRequest request = BuildRequest();
  RankResponse response;

  const ::grpc::Status status =
      ranker.CreateTask(request, &response)->Run();

  ASSERT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(response.status(), RankingServiceStatus::RANKING_SUCCESS);
  EXPECT_EQ(response.ranked_candidate_count(), 2);
  ASSERT_EQ(response.ranked_candidates_size(), 2);

  const RankedCandidateItem& first = response.ranked_candidates(0);
  EXPECT_EQ(first.candidate().item_id(), 1);
  EXPECT_EQ(first.rank_position(), 1);
  EXPECT_EQ(first.ranker(), "heuristic_v1");
  EXPECT_GT(first.rank_score(), response.ranked_candidates(1).rank_score());
  EXPECT_EQ(first.candidate().retrieve_score(), 0.7);
  ASSERT_EQ(first.retrieval_signals_size(), 2);
  EXPECT_EQ(first.retrieval_signals(0).retriever(), "item_cf");
  EXPECT_EQ(first.retrieval_signals(1).retriever(), "user_cf");
  EXPECT_EQ(first.item_features().title(), "Toy Story");
  ASSERT_EQ(first.item_features().genres_size(), 2);
  EXPECT_EQ(first.score_factors_size(), 4);

  EXPECT_EQ(store->requests, (vector<uint64_t>{1, 2}));
}

TEST(HeuristicRankerTest, ReturnsEmptyCandidateStatusWhenAllCandidatesInvalid) {
  shared_ptr<FakeItemIndexStore> store = make_shared<FakeItemIndexStore>();
  HeuristicRanker ranker(store);

  RankRequest request;
  request.set_user_id(42);
  request.mutable_profile()->set_user_id(42);
  request.set_max_results(10);
  request.add_candidates()->CopyFrom(BuildCandidate(0, "item_cf", 1.0));

  RankResponse response;
  const ::grpc::Status status =
      ranker.CreateTask(request, &response)->Run();

  ASSERT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(response.status(),
            RankingServiceStatus::RANKING_EMPTY_CANDIDATES);
  EXPECT_EQ(response.ranked_candidate_count(), 0);
  EXPECT_TRUE(store->requests.empty());
}

}  // namespace
}  // namespace recommendation_engine
