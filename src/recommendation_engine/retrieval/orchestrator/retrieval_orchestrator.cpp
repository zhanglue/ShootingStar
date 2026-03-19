#include "src/recommendation_engine/retrieval/orchestrator/retrieval_orchestrator.h"

#include <format>

namespace recommendation_engine {

using ::grpc::ServerContext;
using ::grpc::Status;
using ::grpc::StatusCode;
using ::std::format;

Status RetrievalOrchestrator::Retrieve(ServerContext* context,
                                       const RetrieveRequest* request,
                                       RetrieveResponse* response) {
  (void)context;

  response->mutable_request()->CopyFrom(*request);

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

  if (request->candidate_count() <= 0) {
    response->set_status(RetrievalServiceStatus::RETRIEVAL_INVALID_REQUEST);
    return Status(StatusCode::INVALID_ARGUMENT,
                  format("Invalid candidate_count {}.", request->candidate_count()));
  }

  return DispatchToRetrievers(*request, response);
}

Status RetrievalOrchestrator::DispatchToRetrievers(const RetrieveRequest& request,
                                                   RetrieveResponse* response) const {
  (void)request;

  response->set_status(RetrievalServiceStatus::RETRIEVAL_SYSTEM_ERROR);

  // TODO: Fan out requests to downstream retrievers in parallel, merge their
  // candidate sets, and populate response->mutable_candidates().
  return Status(StatusCode::UNIMPLEMENTED,
                "TODO: Implement downstream retriever fan-out and merge.");
}

}  // namespace recommendation_engine
