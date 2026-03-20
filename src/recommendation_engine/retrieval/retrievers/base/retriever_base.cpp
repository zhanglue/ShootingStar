#include "src/recommendation_engine/retrieval/retrievers/base/retriever_base.h"

#include <format>

namespace recommendation_engine {

using ::grpc::ServerContext;
using ::grpc::Status;
using ::grpc::StatusCode;
using ::std::format;

RetrieverBase::RetrieverBase(int default_max_candidate_count)
    : default_max_candidate_count_(default_max_candidate_count) {}

Status RetrieverBase::Retrieve(ServerContext* context,
                               const RetrieverRequest* request,
                               RetrieverResponse* response) {
  (void)context;
  response->set_candidate_count(0);

  RetrieverRequest normalized_request;
  normalized_request.CopyFrom(*request);
  if (normalized_request.max_candidate_count() <= 0) {
    normalized_request.set_max_candidate_count(default_max_candidate_count_);
  }
  response->mutable_request()->CopyFrom(normalized_request);

  const Status request_status = IsRequestValid(normalized_request, response);
  if (!request_status.ok()) {
    return request_status;
  }

  const Status retrieve_status = DoRetrieve(normalized_request, response);
  if (retrieve_status.ok()) {
    response->set_candidate_count(response->candidates_size());
  }
  return retrieve_status;
}

Status RetrieverBase::IsRequestValid(const RetrieverRequest& request,
                                     RetrieverResponse* response) const {
  if (request.user_id() <= 0) {
    response->set_status(RetrieverServiceStatus::RETRIEVER_INVALID_REQUEST);
    return Status(StatusCode::INVALID_ARGUMENT,
                  format("Invalid user_id {}.", request.user_id()));
  }

  if (!request.has_profile()) {
    response->set_status(RetrieverServiceStatus::RETRIEVER_INVALID_REQUEST);
    return Status(StatusCode::INVALID_ARGUMENT,
                  format("Profile is required for user {}.", request.user_id()));
  }

  return Status::OK;
}

}  // namespace recommendation_engine
