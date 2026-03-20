#include "src/recommendation_engine/gateway/gateway_service.h"

#include <format>

namespace recommendation_engine {

using ::grpc::ClientContext;
using ::grpc::Status;
using ::grpc::StatusCode;
using ::std::format;

GatewayServiceImpl::GatewayServiceImpl(::std::shared_ptr<::grpc::Channel> profile_channel,
                                       ::std::shared_ptr<::grpc::Channel> retrieval_channel)
    : profile_stub_(ProfileService::NewStub(::std::move(profile_channel))),
      retrieval_stub_(RetrievalService::NewStub(::std::move(retrieval_channel))) {}

::grpc::Status GatewayServiceImpl::Recommend(
    ::grpc::ServerContext* context,
    const RecommendRequest* request,
    RecommendResponse* response) {
  (void)context;

  response->mutable_request()->CopyFrom(*request);
  response->set_candidate_count(0);

  RecommendationStatus recommendation_status = RecommendationStatus::SYSTEM_ERROR;
  Status status = FetchProfile(*request, response->mutable_profile(), &recommendation_status);
  if (!status.ok()) {
    response->set_status(recommendation_status);
    return status;
  }

  status = FetchCandidates(*request, response->profile(), response, &recommendation_status);
  response->set_status(recommendation_status);
  return status;
}

::grpc::Status GatewayServiceImpl::FetchProfile(const RecommendRequest& request, Profile* profile,
                                                RecommendationStatus* recommendation_status) const {
  if (profile_stub_ == nullptr) {
    *recommendation_status = RecommendationStatus::SYSTEM_ERROR;
    return Status(StatusCode::INTERNAL, "Profile service client is not initialized.");
  }

  GetProfileRequest profile_request;
  profile_request.set_request_id(request.request_id());
  profile_request.set_user_id(request.user_id());

  GetProfileResponse profile_response;
  ClientContext profile_context;
  const Status profile_status =
      profile_stub_->GetProfile(&profile_context, profile_request, &profile_response);

  if (!profile_status.ok()) {
    if (profile_status.error_code() == StatusCode::NOT_FOUND ||
        profile_response.status() == ProfileServiceStatus::PROFILE_USER_NOT_FOUND) {
      *recommendation_status = RecommendationStatus::USER_NOT_FOUND;
      return Status(StatusCode::NOT_FOUND, format("User ID of {} not found.", request.user_id()));
    }

    *recommendation_status = RecommendationStatus::SYSTEM_ERROR;
    return Status(StatusCode::INTERNAL,
                  format("Failed to fetch profile for user {}: {}", request.user_id(),
                         profile_status.error_message()));
  }

  if (profile_response.status() != ProfileServiceStatus::PROFILE_SUCCESS) {
    *recommendation_status = profile_response.status() == ProfileServiceStatus::PROFILE_USER_NOT_FOUND
                                 ? RecommendationStatus::USER_NOT_FOUND
                                 : RecommendationStatus::SYSTEM_ERROR;
    return Status(profile_response.status() == ProfileServiceStatus::PROFILE_USER_NOT_FOUND
                      ? StatusCode::NOT_FOUND
                      : StatusCode::INTERNAL,
                  format("Profile service returned non-success status {} for user {}.",
                         static_cast<int>(profile_response.status()), request.user_id()));
  }

  *recommendation_status = RecommendationStatus::SUCCESS;
  profile->CopyFrom(profile_response.profile());
  return ::grpc::Status::OK;
}

::grpc::Status GatewayServiceImpl::FetchCandidates(const RecommendRequest& request,
                                                   const Profile& profile,
                                                   RecommendResponse* response,
                                                   RecommendationStatus* recommendation_status) const {
  if (retrieval_stub_ == nullptr) {
    *recommendation_status = RecommendationStatus::SYSTEM_ERROR;
    return Status(StatusCode::INTERNAL, "Retrieval service client is not initialized.");
  }

  RetrieveRequest retrieval_request;
  retrieval_request.set_request_id(request.request_id());
  retrieval_request.set_user_id(request.user_id());
  retrieval_request.mutable_profile()->CopyFrom(profile);
  retrieval_request.set_max_candidate_count(request.max_candidate_count());

  RetrieveResponse retrieval_response;
  ClientContext retrieval_context;
  const Status retrieval_status =
      retrieval_stub_->Retrieve(&retrieval_context, retrieval_request, &retrieval_response);

  if (!retrieval_status.ok()) {
    *recommendation_status = retrieval_status.error_code() == StatusCode::INVALID_ARGUMENT
                                 ? RecommendationStatus::INVALID_REQUEST
                                 : RecommendationStatus::SYSTEM_ERROR;
    return Status(*recommendation_status == RecommendationStatus::INVALID_REQUEST
                      ? StatusCode::INVALID_ARGUMENT
                      : StatusCode::INTERNAL,
                  format("Failed to fetch retrieval candidates for user {}: {}",
                         request.user_id(), retrieval_status.error_message()));
  }

  if (retrieval_response.status() != RetrievalServiceStatus::RETRIEVAL_SUCCESS) {
    *recommendation_status =
        retrieval_response.status() == RetrievalServiceStatus::RETRIEVAL_INVALID_REQUEST
            ? RecommendationStatus::INVALID_REQUEST
            : RecommendationStatus::SYSTEM_ERROR;
    return Status(*recommendation_status == RecommendationStatus::INVALID_REQUEST
                      ? StatusCode::INVALID_ARGUMENT
                      : StatusCode::INTERNAL,
                  format("Retrieval service returned non-success status {} for user {}.",
                         static_cast<int>(retrieval_response.status()), request.user_id()));
  }

  response->set_candidate_count(retrieval_response.candidate_count());
  for (const CandidateItem& candidate : retrieval_response.candidates()) {
    response->add_candidates()->CopyFrom(candidate);
  }

  *recommendation_status = RecommendationStatus::SUCCESS;
  return Status::OK;
}

}  // namespace recommendation_engine
