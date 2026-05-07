#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "protos/recommendation_engine/ranking.pb.h"
#include "src/recommendation_engine/ranking/item_index_store.h"
#include "src/recommendation_engine/ranking/ranker.h"

namespace recommendation_engine {

class HeuristicRankTask final : public RankTask {
 public:
  HeuristicRankTask(const RankRequest& request,
                    RankResponse* response,
                    ::std::shared_ptr<const ItemIndexStore> item_index_store);

  ::grpc::Status Run() override;

 private:
  using WeightByName = ::std::unordered_map<::std::string, double>;
  using WeightByItemId = ::std::unordered_map<uint64_t, double>;

  struct ScoredCandidate {
    CandidateItem candidate;
    ::std::optional<ItemIndexEntry> item_index_entry;
    double rank_score = 0.0;
    ::std::vector<RankingScoreFactor> score_factors;
  };

  ::std::vector<ScoredCandidate> ScoreCandidates(
      const ::std::vector<CandidateItem>& candidates,
      const ::std::vector<::std::optional<ItemIndexEntry>>&
          item_index_entries) const;
  void AddItemFactors(const ItemIndexEntry& item_index_entry,
                      double* negative_feedback_score,
                      ScoredCandidate* scored_candidate) const;
  void AddFactor(double raw_value,
                 double weight,
                 RankingScoreFactorType factor_type,
                 ::std::string_view name,
                 ::std::string_view description,
                 ScoredCandidate* scored_candidate) const;
  void FillRankedCandidate(const ScoredCandidate& scored_candidate,
                           int rank_position,
                           RankedCandidateItem* ranked_candidate) const;

  const RankRequest& request_;
  RankResponse* response_;
  ::std::shared_ptr<const ItemIndexStore> item_index_store_;
  WeightByName genre_weights_;
  WeightByName tag_weights_;
  WeightByName negative_genre_weights_;
  WeightByName negative_tag_weights_;
  WeightByItemId negative_item_weights_;
};

class HeuristicRanker final : public Ranker {
 public:
  static constexpr ::std::string_view kName = "heuristic_v1";

  explicit HeuristicRanker(
      ::std::shared_ptr<const ItemIndexStore> item_index_store);

  ::std::string_view Name() const override;
  ::std::unique_ptr<RankTask> CreateTask(
      const RankRequest& request,
      RankResponse* response) const override;

 private:
  ::std::shared_ptr<const ItemIndexStore> item_index_store_;
};

}  // namespace recommendation_engine
