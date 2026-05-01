#include "src/recommendation_engine/retrieval/retrievers/base/retriever_base.h"

#include <format>
#include <unordered_set>

#include "src/utilities/logger/logger_registry.h"

namespace recommendation_engine {
namespace {

using ::grpc::ServerContext;
using ::grpc::Status;
using ::grpc::StatusCode;
using ::shooting_star::utilities::LoggerRegistry;
using ::std::format;
using ::std::unordered_set;

void AppendItemIdsToFilterOut(
    const ::google::protobuf::RepeatedPtrField<WeightedItem>& items,
    unordered_set<uint64_t>* item_ids_to_filter_out) {
  for (const WeightedItem& item : items) {
    if (item.item_id() > 0) {
      item_ids_to_filter_out->insert(static_cast<uint64_t>(item.item_id()));
    }
  }
}

void AppendItemIdsToFilterOut(
    const ::google::protobuf::RepeatedField<::int64_t>& items,
    unordered_set<uint64_t>* item_ids_to_filter_out) {
  for (const int64_t item_id : items) {
    if (item_id > 0) {
      item_ids_to_filter_out->insert(static_cast<uint64_t>(item_id));
    }
  }
}

}  // namespace

unordered_set<uint64_t> RetrieverBase::CollectItemIdsToFilterOut(
    const Profile& profile) {
  unordered_set<uint64_t> item_ids_to_filter_out;

  AppendItemIdsToFilterOut(profile.behaviors().recent_liked_items(),
                           &item_ids_to_filter_out);
  AppendItemIdsToFilterOut(profile.behaviors().liked_items(),
                           &item_ids_to_filter_out);
  AppendItemIdsToFilterOut(profile.behaviors().interested_items(),
                           &item_ids_to_filter_out);
  AppendItemIdsToFilterOut(profile.behaviors().rated_items(),
                           &item_ids_to_filter_out);
  AppendItemIdsToFilterOut(profile.negative_feedbacks().items(),
                           &item_ids_to_filter_out);

  return item_ids_to_filter_out;
}

RetrieverBase::RetrieverBase(int default_max_candidate_count)
    : default_max_candidate_count_(default_max_candidate_count) {}

Status RetrieverBase::Retrieve(ServerContext* context,
                               const RetrieverRequest* request,
                               RetrieverResponse* response) {
  const auto logger = LoggerRegistry::Get();
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
    logger.Info(
        "retriever_base_retrieve_response",
        {
            {"grpc_status_code",
             format("{}", static_cast<int>(request_status.error_code()))},
            {"grpc_status_message", request_status.error_message()},
            {"response", response->ShortDebugString()},
        });
    return request_status;
  }

  const Status retrieve_status = DoRetrieve(normalized_request, response);
  if (retrieve_status.ok()) {
    response->set_candidate_count(response->candidates_size());
  }
  logger.Info(
      "retriever_base_retrieve_response",
      {
          {"grpc_status_code",
           format("{}", static_cast<int>(retrieve_status.error_code()))},
          {"grpc_status_message", retrieve_status.error_message()},
          {"response", response->ShortDebugString()},
      });
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
