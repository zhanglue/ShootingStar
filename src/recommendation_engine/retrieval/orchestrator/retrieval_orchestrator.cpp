#include "src/recommendation_engine/retrieval/orchestrator/retrieval_orchestrator.h"

#include <format>
#include <unordered_set>
#include <vector>

namespace recommendation_engine {

using ::grpc::ClientContext;
using ::grpc::ServerContext;
using ::grpc::Status;
using ::grpc::StatusCode;
using ::std::format;
using ::std::string;
using ::std::unordered_set;
using ::std::vector;

bool IsRetrieverResponseStatusAccepted(RetrieverServiceStatus status) {
  return status == RetrieverServiceStatus::RETRIEVER_SUCCESS ||
         status == RetrieverServiceStatus::RETRIEVER_EMPTY_TRIGGER_SEEDS;
}

RetrievalOrchestrator::RetrievalOrchestrator(::std::shared_ptr<::grpc::Channel> item_cf_channel,
                                             ::std::shared_ptr<::grpc::Channel> user_cf_channel)
    : retriever_stubs_() {
  retriever_stubs_.emplace("item_cf", RetrieverService::NewStub(::std::move(item_cf_channel)));
  retriever_stubs_.emplace("user_cf", RetrieverService::NewStub(::std::move(user_cf_channel)));
}

Status RetrievalOrchestrator::Retrieve(ServerContext* context,
                                       const RetrieveRequest* request,
                                       RetrieveResponse* response) {
  (void)context;

  response->mutable_request()->CopyFrom(*request);
  response->set_candidate_count(0);

  if (request->user_id() <= 0) {
    response->set_status(RetrievalServiceStatus::RETRIEVAL_INVALID_REQUEST);
    return Status(StatusCode::INVALID_ARGUMENT,
                  format("Invalid user_id {}.", request->user_id()));
  }

  if (!request->has_profile()) {
    response->set_status(RetrievalServiceStatus::RETRIEVAL_INVALID_REQUEST);
    return Status(StatusCode::INVALID_ARGUMENT,
                  format("Profile is required for user {}.", request->user_id()));
  }

  return DispatchToRetrievers(*request, response);
}

Status RetrievalOrchestrator::FetchCandidatesFromRetriever(
    const char* retriever_name,
    RetrieverService::Stub* retriever_stub,
    const RetrieverRequest& retriever_request,
    RetrieverResponse* retriever_response) const {
  if (retriever_stub == nullptr) {
    return Status(StatusCode::INTERNAL,
                  format("{} retriever client is not initialized.", retriever_name));
  }

  ClientContext retriever_context;
  const Status retriever_status =
      retriever_stub->Retrieve(&retriever_context, retriever_request, retriever_response);

  if (!retriever_status.ok()) {
    return Status(StatusCode::INTERNAL,
                  format("Failed to retrieve candidates from {}: {}", retriever_name,
                         retriever_status.error_message()));
  }

  if (!IsRetrieverResponseStatusAccepted(retriever_response->status())) {
    return Status(StatusCode::INTERNAL,
                  format("{} retriever returned non-success status {}.", retriever_name,
                         static_cast<int>(retriever_response->status())));
  }

  return Status::OK;
}

Status RetrievalOrchestrator::DispatchToRetrievers(const RetrieveRequest& request,
                                                   RetrieveResponse* response) const {
  RetrieverRequest retriever_request;
  retriever_request.set_request_id(request.request_id());
  retriever_request.set_user_id(request.user_id());
  retriever_request.mutable_profile()->CopyFrom(request.profile());
  // In proto3, an unset scalar reads as 0, which downstream retrievers
  // normalize to their own default_max_candidate_count_.
  retriever_request.set_max_candidate_count(request.max_candidate_count());

  vector<string> failure_messages;
  vector<RetrieverResponse> retriever_responses;

  for (const auto& [retriever_name, retriever_stub] : retriever_stubs_) {
    RetrieverResponse retriever_response;
    const Status retriever_status = FetchCandidatesFromRetriever(
        retriever_name.c_str(), retriever_stub.get(), retriever_request, &retriever_response);
    if (!retriever_status.ok()) {
      failure_messages.push_back(format("{}: {}", retriever_name,
                                        retriever_status.error_message()));
      continue;
    }

    retriever_responses.push_back(::std::move(retriever_response));
  }

  if (retriever_responses.empty()) {
    response->set_status(RetrievalServiceStatus::RETRIEVAL_SYSTEM_ERROR);
    if (failure_messages.empty()) {
      return Status(StatusCode::INTERNAL, "No retrievers configured.");
    }

    string error_message = "Failed to retrieve candidates from all retrievers.";
    for (const string& failure_message : failure_messages) {
      error_message += " ";
      error_message += failure_message;
    }
    return Status(StatusCode::INTERNAL, error_message);
  }

  unordered_set<uint64_t> existing_item_ids;
  vector<int> next_candidate_indices(retriever_responses.size(), 0);

  while (response->candidates_size() < request.max_candidate_count()) {
    bool added_candidate_in_round = false;

    for (int response_index = 0;
         response_index < static_cast<int>(retriever_responses.size()) &&
         response->candidates_size() < request.max_candidate_count();
         ++response_index) {
      RetrieverResponse& retriever_response = retriever_responses[response_index];
      int& candidate_index = next_candidate_indices[response_index];

      while (candidate_index < retriever_response.candidates_size()) {
        const CandidateItem& candidate = retriever_response.candidates(candidate_index++);
        if (!existing_item_ids.insert(candidate.item_id()).second) {
          continue;
        }

        response->add_candidates()->CopyFrom(candidate);
        added_candidate_in_round = true;
        break;
      }
    }

    if (!added_candidate_in_round) {
      break;
    }
  }

  response->set_candidate_count(response->candidates_size());
  response->set_status(RetrievalServiceStatus::RETRIEVAL_SUCCESS);
  return Status::OK;
}

}  // namespace recommendation_engine
