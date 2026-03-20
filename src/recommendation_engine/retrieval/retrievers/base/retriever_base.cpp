#include "src/recommendation_engine/retrieval/retrievers/base/retriever_base.h"

#include <format>

namespace recommendation_engine {

using ::grpc::ServerContext;
using ::grpc::Status;
using ::grpc::StatusCode;
using ::std::format;

Status RetrieverBase::Retrieve(ServerContext* context,
                               const RetrieverRequest* request,
                               RetrieverResponse* response) {
  (void)context;

  response->mutable_request()->CopyFrom(*request);

  const Status request_status = IsRequestValid(*request, response);
  if (!request_status.ok()) {
    return request_status;
  }

  return DoRetrieve(*request, response);
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

  if (request.candidate_count() <= 0) {
    response->set_status(RetrieverServiceStatus::RETRIEVER_INVALID_REQUEST);
    return Status(StatusCode::INVALID_ARGUMENT,
                  format("Invalid candidate_count {}.", request.candidate_count()));
  }

  return Status::OK;
}

}  // namespace recommendation_engine
