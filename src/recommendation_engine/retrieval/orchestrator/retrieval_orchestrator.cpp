#include "src/recommendation_engine/retrieval/orchestrator/retrieval_orchestrator.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "src/utilities/global_config/global_config.h"
#include "src/utilities/logger/logger_registry.h"
#include "src/utilities/runtime_utilities/runtime_utilities.h"

namespace recommendation_engine {

using ::grpc::Channel;
using ::grpc::ClientContext;
using ::grpc::CreateChannel;
using ::grpc::InsecureChannelCredentials;
using ::grpc::ServerContext;
using ::grpc::Status;
using ::grpc::StatusCode;
using ::shooting_star::utilities::GlobalConfig;
using ::shooting_star::utilities::LoggerRegistry;
using ::std::format;
using ::std::invalid_argument;
using ::std::make_unique;
using ::std::shared_ptr;
using ::std::string;
using ::std::to_string;
using ::std::unique_ptr;
using ::std::unordered_map;
using ::std::unordered_set;
using ::std::vector;

bool IsRetrieverResponseStatusAccepted(RetrieverServiceStatus status) {
  return status == RetrieverServiceStatus::RETRIEVER_SUCCESS ||
         status == RetrieverServiceStatus::RETRIEVER_EMPTY_TRIGGER_SEEDS;
}

int ComputeRetrieverMaxCandidateCount(int max_results, double expand_ratio) {
  if (max_results <= 0) {
    return max_results;
  }

  const int expanded_count = static_cast<int>(
      ::std::ceil(static_cast<double>(max_results) *
                  expand_ratio));
  return ::std::max(max_results, expanded_count);
}

string JoinRetrieverNames(const vector<string>& retriever_names) {
  string joined;
  for (const string& retriever_name : retriever_names) {
    if (retriever_name.empty()) {
      continue;
    }
    if (!joined.empty()) {
      joined += ",";
    }
    joined += retriever_name;
  }
  return joined;
}

void EnsureRetrievalSignals(CandidateItem* candidate) {
  if (candidate->retrieval_signals_size() > 0) {
    return;
  }

  RetrievalSignal* signal = candidate->add_retrieval_signals();
  signal->set_retriever(candidate->retriever());
  signal->set_retrieve_score(candidate->retrieve_score());
  for (const RetrievalReason& reason : candidate->reasons()) {
    signal->add_reasons()->CopyFrom(reason);
  }
}

void RebuildLegacyCandidateFields(CandidateItem* candidate) {
  double aggregate_retrieve_score = 0.0;
  vector<string> retriever_names;
  unordered_set<string> seen_retrievers;

  candidate->clear_reasons();
  for (const RetrievalSignal& signal : candidate->retrieval_signals()) {
    aggregate_retrieve_score += signal.retrieve_score();
    if (!signal.retriever().empty() &&
        seen_retrievers.insert(signal.retriever()).second) {
      retriever_names.push_back(signal.retriever());
    }
    for (const RetrievalReason& reason : signal.reasons()) {
      candidate->add_reasons()->CopyFrom(reason);
    }
  }

  candidate->set_retriever(JoinRetrieverNames(retriever_names));
  candidate->set_retrieve_score(aggregate_retrieve_score);
}

void NormalizeCandidate(CandidateItem* candidate) {
  EnsureRetrievalSignals(candidate);
  RebuildLegacyCandidateFields(candidate);
}

void MergeRetrievalSignal(const RetrievalSignal& source_signal,
                          CandidateItem* target_candidate) {
  for (RetrievalSignal& target_signal :
       *target_candidate->mutable_retrieval_signals()) {
    if (target_signal.retriever() != source_signal.retriever()) {
      continue;
    }

    target_signal.set_retrieve_score(
        target_signal.retrieve_score() + source_signal.retrieve_score());
    for (const RetrievalReason& reason : source_signal.reasons()) {
      target_signal.add_reasons()->CopyFrom(reason);
    }
    return;
  }

  target_candidate->add_retrieval_signals()->CopyFrom(source_signal);
}

void MergeCandidate(const CandidateItem& source_candidate,
                    CandidateItem* target_candidate) {
  CandidateItem normalized_source;
  normalized_source.CopyFrom(source_candidate);
  NormalizeCandidate(&normalized_source);

  if (target_candidate->item_type() == ItemType::ITEM_TYPE_UNSPECIFIED &&
      normalized_source.item_type() != ItemType::ITEM_TYPE_UNSPECIFIED) {
    target_candidate->set_item_type(normalized_source.item_type());
  }
  if (target_candidate->author_id() == 0 &&
      normalized_source.author_id() != 0) {
    target_candidate->set_author_id(normalized_source.author_id());
  }

  for (const RetrievalSignal& source_signal :
       normalized_source.retrieval_signals()) {
    MergeRetrievalSignal(source_signal, target_candidate);
  }
  RebuildLegacyCandidateFields(target_candidate);
}

RetrievalOrchestrator::RetrievalOrchestrator(
    RetrieverStubList retriever_stubs, double recall_candidate_expand_ratio)
    : retriever_stubs_(::std::move(retriever_stubs)),
      recall_candidate_expand_ratio_(recall_candidate_expand_ratio) {
  if (retriever_stubs_.empty()) {
    throw invalid_argument(
        "RetrievalOrchestrator retriever_stubs must not be empty.");
  }
  if (recall_candidate_expand_ratio_ <= 0.0) {
    throw invalid_argument(
        "RetrievalOrchestrator recall_candidate_expand_ratio must be "
        "positive.");
  }
  for (const auto& [retriever_name, retriever_stub] : retriever_stubs_) {
    if (retriever_name.empty()) {
      throw invalid_argument(
          "RetrievalOrchestrator retriever name must not be empty.");
    }
    if (retriever_stub == nullptr) {
      throw invalid_argument(
          format("RetrievalOrchestrator {} retriever_stub must not be null.",
                 retriever_name));
    }
  }
}

unique_ptr<RetrievalOrchestrator> RetrievalOrchestrator::Create(
    const GlobalConfig& config) {
  const string item_cf_service_address = config.GetRetrieverItemCfAddress();
  const string user_cf_service_address = config.GetRetrieverUserCfAddress();
  const double recall_candidate_expand_ratio =
      config.GetRetrievalRecallCandidateExpandRatio();

  LoggerRegistry::Get().Info(
      "retrieval_orchestrator_downstream_clients_configured",
      {
          {"retriever_item_cf_address", item_cf_service_address},
          {"retriever_user_cf_address", user_cf_service_address},
          {"recall_candidate_expand_ratio",
           to_string(recall_candidate_expand_ratio)},
      });

  shared_ptr<Channel> item_cf_channel =
      CreateChannel(item_cf_service_address, InsecureChannelCredentials());
  shared_ptr<Channel> user_cf_channel =
      CreateChannel(user_cf_service_address, InsecureChannelCredentials());

  unique_ptr<RetrieverService::Stub> item_cf_stub =
      RetrieverService::NewStub(::std::move(item_cf_channel));
  unique_ptr<RetrieverService::Stub> user_cf_stub =
      RetrieverService::NewStub(::std::move(user_cf_channel));

  RetrieverStubList retriever_stubs;
  retriever_stubs.emplace_back("item_cf", ::std::move(item_cf_stub));
  retriever_stubs.emplace_back("user_cf", ::std::move(user_cf_stub));

  unique_ptr<RetrievalOrchestrator> server = make_unique<RetrievalOrchestrator>(
      ::std::move(retriever_stubs), recall_candidate_expand_ratio);
  return server;
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
    return Status(
      StatusCode::INVALID_ARGUMENT,
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
    return Status(
      StatusCode::INTERNAL,
      format("{} retriever client is not initialized.", retriever_name));
  }

  ClientContext retriever_context;
  const Status retriever_status =
      retriever_stub->Retrieve(&retriever_context,
                               retriever_request,
                               retriever_response);

  if (!retriever_status.ok()) {
    return Status(
      StatusCode::INTERNAL,
      format("Failed to retrieve candidates from {}: {}", retriever_name,
        retriever_status.error_message()));
  }

  if (!IsRetrieverResponseStatusAccepted(retriever_response->status())) {
    return Status(
      StatusCode::INTERNAL,
      format("{} retriever returned non-success status {}.", retriever_name,
        static_cast<int>(retriever_response->status())));
  }

  return Status::OK;
}

Status RetrievalOrchestrator::DispatchToRetrievers(const RetrieveRequest& request,
                                                   RetrieveResponse* response) const {
  RetrieverRequest retriever_request;
  retriever_request.set_trace_id(request.trace_id());
  retriever_request.set_request_id(::shooting_star::utilities::GenerateGuid());
  retriever_request.set_user_id(request.user_id());
  retriever_request.mutable_profile()->CopyFrom(request.profile());
  retriever_request.set_max_candidate_count(
      ComputeRetrieverMaxCandidateCount(
          request.max_candidate_count(),
          recall_candidate_expand_ratio_));

  vector<string> failure_messages;
  vector<RetrieverResponse> retriever_responses;

  for (const auto& [retriever_name, retriever_stub] : retriever_stubs_) {
    RetrieverResponse retriever_response;
    const Status retriever_status = FetchCandidatesFromRetriever(
        retriever_name.c_str(),
        retriever_stub.get(),
        retriever_request,
        &retriever_response);
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

  unordered_map<uint64_t, int> candidate_index_by_item_id;
  vector<int> next_candidate_indices(retriever_responses.size(), 0);

  while (true) {
    bool advanced_in_round = false;

    for (int response_index = 0;
         response_index < static_cast<int>(retriever_responses.size());
         ++response_index) {
      RetrieverResponse& retriever_response = retriever_responses[response_index];
      int& candidate_index = next_candidate_indices[response_index];

      if (candidate_index >= retriever_response.candidates_size()) {
        continue;
      }

      const CandidateItem& candidate =
          retriever_response.candidates(candidate_index++);
      advanced_in_round = true;
      if (candidate.item_id() == 0) {
        continue;
      }

      const auto existing_candidate_iter =
          candidate_index_by_item_id.find(candidate.item_id());
      if (existing_candidate_iter != candidate_index_by_item_id.end()) {
        MergeCandidate(
            candidate,
            response->mutable_candidates(existing_candidate_iter->second));
        continue;
      }

      if (response->candidates_size() < request.max_candidate_count()) {
        CandidateItem* added_candidate = response->add_candidates();
        added_candidate->CopyFrom(candidate);
        NormalizeCandidate(added_candidate);
        candidate_index_by_item_id.emplace(
            added_candidate->item_id(), response->candidates_size() - 1);
      }
    }

    if (!advanced_in_round) {
      break;
    }
  }

  response->set_candidate_count(response->candidates_size());
  response->set_status(RetrievalServiceStatus::RETRIEVAL_SUCCESS);
  return Status::OK;
}

}  // namespace recommendation_engine
