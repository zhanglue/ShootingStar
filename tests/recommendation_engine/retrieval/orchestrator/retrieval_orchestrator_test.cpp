#include "src/recommendation_engine/retrieval/orchestrator/retrieval_orchestrator.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>

namespace recommendation_engine {
namespace {

using ::grpc::Channel;
using ::grpc::CreateChannel;
using ::grpc::InsecureChannelCredentials;
using ::grpc::InsecureServerCredentials;
using ::grpc::Server;
using ::grpc::ServerBuilder;
using ::grpc::ServerContext;
using ::grpc::Status;
using ::std::shared_ptr;
using ::std::string;
using ::std::unique_ptr;

class FakeRetrieverService final : public RetrieverService::Service {
 public:
  Status Retrieve(ServerContext* context,
                  const RetrieverRequest* request,
                  RetrieverResponse* response) override {
    (void)context;
    ++request_count;
    last_request.CopyFrom(*request);
    response->CopyFrom(response_to_return);
    return grpc_status;
  }

  RetrieverResponse response_to_return;
  RetrieverRequest last_request;
  Status grpc_status = Status::OK;
  int request_count = 0;
};

class FakeRetrieverServer {
 public:
  explicit FakeRetrieverServer(FakeRetrieverService* server) {
    int selected_port = 0;
    ServerBuilder builder;
    builder.AddListeningPort("127.0.0.1:0",
                             InsecureServerCredentials(),
                             &selected_port);
    builder.RegisterService(server);
    server_ = builder.BuildAndStart();
    address_ = "127.0.0.1:" + ::std::to_string(selected_port);
  }

  ~FakeRetrieverServer() {
    if (server_ != nullptr) {
      server_->Shutdown();
    }
  }

  shared_ptr<Channel> CreateClientChannel() const {
    return CreateChannel(address_, InsecureChannelCredentials());
  }

 private:
  string address_;
  unique_ptr<Server> server_;
};

RetrievalReason* AddReason(CandidateItem* candidate,
                           ReasonType reason_type,
                           EntityType trigger_type,
                           uint64_t trigger_id,
                           double reason_score) {
  RetrievalReason* reason = candidate->add_reasons();
  reason->set_reason_type(reason_type);
  reason->set_reason_score(reason_score);
  reason->mutable_trigger()->set_entity_type(trigger_type);
  reason->mutable_trigger()->set_entity_id(trigger_id);
  return reason;
}

CandidateItem* AddLegacyCandidate(RetrieverResponse* response,
                                  uint64_t item_id,
                                  const string& retriever,
                                  double retrieve_score,
                                  ReasonType reason_type,
                                  EntityType trigger_type,
                                  uint64_t trigger_id) {
  CandidateItem* candidate = response->add_candidates();
  candidate->set_item_id(item_id);
  candidate->set_item_type(ItemType::ITEM_TYPE_VIDEO);
  candidate->set_retriever(retriever);
  candidate->set_retrieve_score(retrieve_score);
  AddReason(candidate, reason_type, trigger_type, trigger_id, retrieve_score);
  response->set_candidate_count(response->candidates_size());
  return candidate;
}

RetrieveRequest BuildRequest(int max_candidate_count) {
  RetrieveRequest request;
  request.set_trace_id("trace-1");
  request.set_request_id("request-1");
  request.set_user_id(42);
  request.set_max_candidate_count(max_candidate_count);
  request.mutable_profile()->set_user_id(42);
  return request;
}

TEST(RetrievalOrchestratorTest,
     MergesDuplicateCandidatesIntoRetrievalSignalsAfterTopKIsFull) {
  FakeRetrieverService item_cf_service;
  item_cf_service.response_to_return.set_status(
      RetrieverServiceStatus::RETRIEVER_SUCCESS);
  AddLegacyCandidate(&item_cf_service.response_to_return,
                     10,
                     "item_cf",
                     1.0,
                     ReasonType::REASON_TYPE_ITEM_CF,
                     EntityType::ENTITY_TYPE_ITEM,
                     100);
  AddLegacyCandidate(&item_cf_service.response_to_return,
                     20,
                     "item_cf",
                     0.8,
                     ReasonType::REASON_TYPE_ITEM_CF,
                     EntityType::ENTITY_TYPE_ITEM,
                     200);

  FakeRetrieverService user_cf_service;
  user_cf_service.response_to_return.set_status(
      RetrieverServiceStatus::RETRIEVER_SUCCESS);
  AddLegacyCandidate(&user_cf_service.response_to_return,
                     40,
                     "user_cf",
                     0.7,
                     ReasonType::REASON_TYPE_USER_CF,
                     EntityType::ENTITY_TYPE_USER,
                     900);
  AddLegacyCandidate(&user_cf_service.response_to_return,
                     10,
                     "user_cf",
                     0.5,
                     ReasonType::REASON_TYPE_USER_CF,
                     EntityType::ENTITY_TYPE_USER,
                     901);

  FakeRetrieverServer item_cf_server(&item_cf_service);
  FakeRetrieverServer user_cf_server(&user_cf_service);
  RetrievalOrchestrator::RetrieverStubList retriever_stubs;
  retriever_stubs.emplace_back(
      "item_cf",
      RetrieverService::NewStub(item_cf_server.CreateClientChannel()));
  retriever_stubs.emplace_back(
      "user_cf",
      RetrieverService::NewStub(user_cf_server.CreateClientChannel()));
  RetrievalOrchestrator orchestrator(::std::move(retriever_stubs), 1.0);

  RetrieveRequest request = BuildRequest(2);
  RetrieveResponse response;
  const Status status = orchestrator.Retrieve(nullptr, &request, &response);

  EXPECT_TRUE(status.ok());
  EXPECT_EQ(response.status(), RetrievalServiceStatus::RETRIEVAL_SUCCESS);
  EXPECT_EQ(item_cf_service.request_count, 1);
  EXPECT_EQ(user_cf_service.request_count, 1);
  ASSERT_EQ(response.candidates_size(), 2);
  EXPECT_EQ(response.candidate_count(), 2);

  const CandidateItem& merged_candidate = response.candidates(0);
  EXPECT_EQ(merged_candidate.item_id(), 10);
  EXPECT_EQ(merged_candidate.retriever(), "item_cf,user_cf");
  EXPECT_DOUBLE_EQ(merged_candidate.retrieve_score(), 1.5);
  ASSERT_EQ(merged_candidate.reasons_size(), 2);
  EXPECT_EQ(merged_candidate.reasons(0).reason_type(),
            ReasonType::REASON_TYPE_ITEM_CF);
  EXPECT_EQ(merged_candidate.reasons(1).reason_type(),
            ReasonType::REASON_TYPE_USER_CF);

  ASSERT_EQ(merged_candidate.retrieval_signals_size(), 2);
  EXPECT_EQ(merged_candidate.retrieval_signals(0).retriever(), "item_cf");
  EXPECT_DOUBLE_EQ(merged_candidate.retrieval_signals(0).retrieve_score(),
                   1.0);
  ASSERT_EQ(merged_candidate.retrieval_signals(0).reasons_size(), 1);
  EXPECT_EQ(
      merged_candidate.retrieval_signals(0).reasons(0).trigger().entity_id(),
      100);

  EXPECT_EQ(merged_candidate.retrieval_signals(1).retriever(), "user_cf");
  EXPECT_DOUBLE_EQ(merged_candidate.retrieval_signals(1).retrieve_score(),
                   0.5);
  ASSERT_EQ(merged_candidate.retrieval_signals(1).reasons_size(), 1);
  EXPECT_EQ(
      merged_candidate.retrieval_signals(1).reasons(0).trigger().entity_id(),
      901);

  const CandidateItem& second_candidate = response.candidates(1);
  EXPECT_EQ(second_candidate.item_id(), 40);
  EXPECT_EQ(second_candidate.retriever(), "user_cf");
  EXPECT_DOUBLE_EQ(second_candidate.retrieve_score(), 0.7);
  ASSERT_EQ(second_candidate.retrieval_signals_size(), 1);
  EXPECT_EQ(second_candidate.retrieval_signals(0).retriever(), "user_cf");
}

}  // namespace
}  // namespace recommendation_engine
