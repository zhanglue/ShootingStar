#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include <grpcpp/grpcpp.h>

#include "protos/recommendation_engine/retriever.grpc.pb.h"
#include "protos/recommendation_engine/retrieval.grpc.pb.h"

namespace recommendation_engine {

class RetrievalOrchestrator final : public RetrievalService::Service {
 public:
  RetrievalOrchestrator(::std::shared_ptr<::grpc::Channel> item_cf_channel,
                        ::std::shared_ptr<::grpc::Channel> user_cf_channel);

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

  using RetrieverStubMap =
      ::std::unordered_map<::std::string, ::std::unique_ptr<RetrieverService::Stub>>;

  RetrieverStubMap retriever_stubs_;
};

}  // namespace recommendation_engine
