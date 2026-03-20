#pragma once

#include <grpcpp/grpcpp.h>

#include "protos/recommendation_engine/retriever.grpc.pb.h"

namespace recommendation_engine {

class RetrieverBase : public RetrieverService::Service {
 public:
  explicit RetrieverBase(int default_max_candidate_count = 5);
  ~RetrieverBase() override = default;

  ::grpc::Status Retrieve(::grpc::ServerContext* context,
                          const RetrieverRequest* request,
                          RetrieverResponse* response) final;

 protected:
  virtual ::grpc::Status IsRequestValid(const RetrieverRequest& request,
                                        RetrieverResponse* response) const;

 private:
  virtual ::grpc::Status DoRetrieve(const RetrieverRequest& request,
                                    RetrieverResponse* response) const = 0;

  int default_max_candidate_count_;
};

}  // namespace recommendation_engine
