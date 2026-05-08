#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "src/recommendation_engine/retrieval/retrievers/user_cf/profile_store.h"
#include "src/recommendation_engine/retrieval/retrievers/base/retriever_base.h"
#include "src/recommendation_engine/retrieval/retrievers/user_cf/user_similarity_store.h"
#include "src/utilities/global_config/global_config.h"

namespace recommendation_engine {

class RetrieverUserCf final : public RetrieverBase {
 public:
  struct Options {
    int default_max_candidate_count = 10;
    int trigger_seed_user_count = 10;
    double score_multiplier = 1.0;
  };

  RetrieverUserCf(::std::unique_ptr<user_cf::UserSimilarityStore> user_similarity_store,
                  ::std::unique_ptr<user_cf::ProfileStore> profile_store,
                  Options options);

  static ::std::unique_ptr<RetrieverUserCf> Create(
      const ::shooting_star::utilities::GlobalConfig& config);

 private:
  // Shared mutable state for one retrieval request.
  struct SessionData {
    ::std::string trace_id;
    ::std::vector<RetrieverBase::TriggerSeed> trigger_seeds;
    ::std::vector<uint64_t> trigger_user_ids;
    ::std::unordered_set<uint64_t> item_ids_to_filter_out;
    ::std::vector<::std::optional<UserCfProfile>> trigger_user_profiles;
    ::std::unordered_map<uint64_t, RetrieverBase::CandidateScore> candidates;
    ::std::size_t missing_profile_count = 0;
    ::std::size_t total_neighbor_items_seen = 0;
    ::std::size_t total_neighbor_items_filtered_out = 0;
  };

  // Main retrieval pipeline: similar-user seeds -> profile fetch -> aggregation -> response.
  ::grpc::Status DoRetrieve(const RetrieverRequest& request,
                            RetrieverResponse* response) const override;

  // Load similar users and convert them into trigger seeds.
  ::grpc::Status LoadTriggerSeeds(
      const RetrieverRequest& request,
      const ::std::shared_ptr<SessionData>& session,
      RetrieverResponse* response) const;
  // Build item ids that should never be recommended back to the requester.
  void BuildFilterSet(
      const Profile& profile,
      const ::std::shared_ptr<SessionData>& session) const;
  // Fetch trigger users' profiles in batch for candidate extraction.
  ::grpc::Status LoadTriggerUserProfiles(
      const RetrieverRequest& request,
      const ::std::shared_ptr<SessionData>& session,
      RetrieverResponse* response) const;
  // Aggregate candidate contributions from trigger-user item lists.
  void AggregateCandidates(const ::std::shared_ptr<SessionData>& session) const;
  // Sort candidate pool and materialize the final response list.
  void FillResponseCandidates(
      const ::std::shared_ptr<SessionData>& session,
      const RetrieverRequest& request,
      RetrieverResponse* response) const;

  // Convert raw user neighbors into valid, deduplicated trigger seeds.
  static ::std::vector<RetrieverBase::TriggerSeed> CollectTriggerSeeds(
      uint64_t request_user_id,
      const ::std::vector<user_cf::UserNeighbor>& user_neighbors);

  ::std::unique_ptr<user_cf::UserSimilarityStore> user_similarity_store_;
  ::std::unique_ptr<user_cf::ProfileStore> profile_store_;
  int trigger_seed_user_count_ = 10;
  double score_multiplier_ = 1.0;
};

}  // namespace recommendation_engine
