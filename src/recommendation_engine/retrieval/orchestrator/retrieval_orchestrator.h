#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "protos/recommendation_engine/retriever.grpc.pb.h"
#include "protos/recommendation_engine/retrieval.grpc.pb.h"
#include "src/utilities/global_config/global_config.h"

namespace recommendation_engine {

class RetrievalOrchestrator final : public RetrievalService::Service {
 public:
  using NamedRetrieverStub =
      ::std::pair<::std::string, ::std::unique_ptr<RetrieverService::Stub>>;
  using RetrieverStubList = ::std::vector<NamedRetrieverStub>;

  RetrievalOrchestrator(RetrieverStubList retriever_stubs,
                        double recall_candidate_expand_ratio);

  static ::std::unique_ptr<RetrievalOrchestrator> Create(
      const ::shooting_star::utilities::GlobalConfig& config);

  ::grpc::Status Retrieve(::grpc::ServerContext* context,
                          const RetrieveRequest* request,
                          RetrieveResponse* response) override;

 private:
  ::grpc::Status FetchCandidatesFromRetriever(const char* retriever_name,
                                              RetrieverService::Stub* retriever_stub,
                                              const RetrieverRequest& retriever_request,
                                              RetrieverResponse* retriever_response) const;

  ::grpc::Status DispatchToRetrievers(const RetrieveRequest& request,
                                      RetrieveResponse* response) const;

  RetrieverStubList retriever_stubs_;
  double recall_candidate_expand_ratio_ = 1.0;
};

}  // namespace recommendation_engine
