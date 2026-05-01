#pragma once

#include <grpcpp/grpcpp.h>

#include <cstdint>
#include <unordered_set>

#include "protos/recommendation_engine/retriever.grpc.pb.h"

namespace recommendation_engine {

class RetrieverBase : public RetrieverService::Service {
 public:
  struct TriggerSeed {
    uint64_t entity_id = 0;
    double score = 0.0;
  };

  struct ScoredItem {
    uint64_t item_id = 0;
    double score = 0.0;
  };

  struct CandidateScore {
    uint64_t item_id = 0;
    double score = 0.0;
    uint64_t trigger_entity_id = 0;
    double trigger_score = 0.0;
    double source_score = 0.0;
    double reason_score = 0.0;
  };

  explicit RetrieverBase(int default_max_candidate_count = 5);
  ~RetrieverBase() override = default;

  ::grpc::Status Retrieve(::grpc::ServerContext* context,
                          const RetrieverRequest* request,
                          RetrieverResponse* response) final;

 protected:
  virtual ::grpc::Status IsRequestValid(const RetrieverRequest& request,
                                        RetrieverResponse* response) const;
  static ::std::unordered_set<uint64_t> CollectItemIdsToFilterOut(
      const Profile& profile);

 private:
  virtual ::grpc::Status DoRetrieve(const RetrieverRequest& request,
                                    RetrieverResponse* response) const = 0;

  int default_max_candidate_count_;
};

}  // namespace recommendation_engine
