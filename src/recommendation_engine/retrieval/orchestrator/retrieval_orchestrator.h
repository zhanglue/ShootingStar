#pragma once

#include <grpcpp/grpcpp.h>

#include "protos/recommendation_engine/retrieval.grpc.pb.h"

namespace recommendation_engine {

class RetrievalOrchestrator final : public RetrievalService::Service {
 public:
  RetrievalOrchestrator() = default;

  ::grpc::Status Retrieve(::grpc::ServerContext* context,
                          const RetrieveRequest* request,
                          RetrieveResponse* response) override;

 private:
  ::grpc::Status DispatchToRetrievers(const RetrieveRequest& request,
                                      RetrieveResponse* response) const;
};

}  // namespace recommendation_engine
