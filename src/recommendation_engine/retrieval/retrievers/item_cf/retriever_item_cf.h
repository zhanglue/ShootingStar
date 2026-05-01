#pragma once

#include <cstddef>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "src/recommendation_engine/retrieval/retrievers/item_cf/item_similarity_store.h"
#include "src/recommendation_engine/retrieval/retrievers/base/retriever_base.h"

namespace recommendation_engine {

class RetrieverItemCf final : public RetrieverBase {
 public:
  explicit RetrieverItemCf(int default_max_candidate_count = 10);
  RetrieverItemCf(::std::unique_ptr<ItemSimilarityStore> item_similarity_store,
                  int default_max_candidate_count = 10);

 private:
  // Shared mutable state for one retrieval request.
  struct SessionData {
    ::std::vector<RetrieverBase::TriggerSeed> trigger_seeds;
    ::std::unordered_set<uint64_t> item_ids_to_filter_out;
    ::std::unordered_map<uint64_t, RetrieverBase::CandidateScore> candidates;
    ::std::size_t total_neighbors_fetched = 0;
    ::std::size_t total_neighbors_filtered_out = 0;
  };

  // Main retrieval pipeline: seed building -> filtering -> aggregation -> response.
  ::grpc::Status DoRetrieve(const RetrieverRequest& request,
                            RetrieverResponse* response) const override;

  // Build and truncate trigger seeds from user profile behaviors.
  void BuildTriggerSeeds(
      const RetrieverRequest& request,
      const ::std::shared_ptr<SessionData>& session) const;
  // Build item ids that should never be returned (e.g. interacted/history items).
  void BuildFilterSet(
      const Profile& profile,
      const ::std::shared_ptr<SessionData>& session) const;
  // Query similarity store in batches and merge neighbor contributions.
  ::grpc::Status AggregateSimilarityCandidates(
      const RetrieverRequest& request,
      const ::std::shared_ptr<SessionData>& session,
      RetrieverResponse* response) const;
  // Aggregate one lookup batch into the global candidate pool.
  void AggregateSimilarityBatch(
      const ::std::vector<::std::vector<ItemNeighbor>>& neighbors_by_item_id,
      ::std::size_t batch_start,
      const ::std::shared_ptr<SessionData>& session,
      ::std::size_t* batch_neighbors_fetched,
      ::std::size_t* batch_neighbors_filtered_out) const;
  // Sort candidate pool and materialize the final response list.
  void FillResponseCandidates(
      const ::std::shared_ptr<SessionData>& session,
      const RetrieverRequest& request,
      RetrieverResponse* response) const;

  // Collect raw seeds from profile channels before top-K truncation.
  static ::std::vector<RetrieverBase::TriggerSeed> CollectTriggerSeeds(
      const Profile& profile);

  ::std::unique_ptr<ItemSimilarityStore> item_similarity_store_;
};

}  // namespace recommendation_engine
