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

namespace recommendation_engine {

class RetrieverUserCf final : public RetrieverBase {
 public:
  explicit RetrieverUserCf(int default_max_candidate_count = 10);
  RetrieverUserCf(::std::unique_ptr<user_cf::UserSimilarityStore> user_similarity_store,
                  ::std::unique_ptr<user_cf::ProfileStore> profile_store,
                  int default_max_candidate_count = 10);

 private:
  struct SessionData {
    ::std::vector<RetrieverBase::TriggerSeed> trigger_seeds;
    ::std::vector<uint64_t> trigger_user_ids;
    ::std::unordered_set<uint64_t> item_ids_to_filter_out;
    ::std::vector<::std::optional<UserCfProfile>> trigger_user_profiles;
    ::std::unordered_map<uint64_t, RetrieverBase::CandidateScore> candidates;
    ::std::size_t missing_profile_count = 0;
    ::std::size_t total_neighbor_items_seen = 0;
    ::std::size_t total_neighbor_items_filtered_out = 0;
  };

  ::grpc::Status DoRetrieve(const RetrieverRequest& request,
                            RetrieverResponse* response) const override;

  ::grpc::Status LoadTriggerSeeds(
      const RetrieverRequest& request,
      const ::std::shared_ptr<SessionData>& session,
      RetrieverResponse* response) const;
  void BuildFilterSet(
      const Profile& profile,
      const ::std::shared_ptr<SessionData>& session) const;
  ::grpc::Status LoadTriggerUserProfiles(
      const RetrieverRequest& request,
      const ::std::shared_ptr<SessionData>& session,
      RetrieverResponse* response) const;
  void AggregateCandidates(const ::std::shared_ptr<SessionData>& session) const;
  void FillResponseCandidates(
      const ::std::shared_ptr<SessionData>& session,
      const RetrieverRequest& request,
      RetrieverResponse* response) const;

  static ::std::vector<RetrieverBase::TriggerSeed> CollectTriggerSeeds(
      uint64_t request_user_id,
      const ::std::vector<user_cf::UserNeighbor>& user_neighbors);

  ::std::unique_ptr<user_cf::UserSimilarityStore> user_similarity_store_;
  ::std::unique_ptr<user_cf::ProfileStore> profile_store_;
};

}  // namespace recommendation_engine
