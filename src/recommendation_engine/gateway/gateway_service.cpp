#include "src/recommendation_engine/gateway/gateway_service.h"

#include <format>

namespace recommendation_engine {

using ::grpc::ClientContext;
using ::grpc::Status;
using ::grpc::StatusCode;
using ::std::format;

GatewayServiceImpl::GatewayServiceImpl(::std::shared_ptr<::grpc::Channel> profile_channel)
    : profile_stub_(ProfileService::NewStub(::std::move(profile_channel))) {}

::grpc::Status GatewayServiceImpl::Recommend(
    ::grpc::ServerContext* context,
    const RecommendRequest* request,
    RecommendResponse* response) {
  (void)context;

  response->mutable_request()->CopyFrom(*request);

  RecommenderStatus recommender_status = RecommenderStatus::SYSTEM_ERROR;
  Status status = FetchProfile(*request, response->mutable_profile(), &recommender_status);
  response->set_status(recommender_status);
  return status;
}

::grpc::Status GatewayServiceImpl::FetchProfile(const RecommendRequest& request, Profile* profile,
                                                RecommenderStatus* recommender_status) const {
  if (profile_stub_ == nullptr) {
    *recommender_status = RecommenderStatus::SYSTEM_ERROR;
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
      *recommender_status = RecommenderStatus::USER_NOT_FOUND;
      return Status(StatusCode::NOT_FOUND, format("User ID of {} not found.", request.user_id()));
    }

    *recommender_status = RecommenderStatus::SYSTEM_ERROR;
    return Status(StatusCode::INTERNAL,
                  format("Failed to fetch profile for user {}: {}", request.user_id(),
                         profile_status.error_message()));
  }

  if (profile_response.status() != ProfileServiceStatus::PROFILE_SUCCESS) {
    *recommender_status = profile_response.status() == ProfileServiceStatus::PROFILE_USER_NOT_FOUND
                              ? RecommenderStatus::USER_NOT_FOUND
                              : RecommenderStatus::SYSTEM_ERROR;
    return Status(profile_response.status() == ProfileServiceStatus::PROFILE_USER_NOT_FOUND
                      ? StatusCode::NOT_FOUND
                      : StatusCode::INTERNAL,
                  format("Profile service returned non-success status {} for user {}.",
                         static_cast<int>(profile_response.status()), request.user_id()));
  }

  *recommender_status = RecommenderStatus::SUCCESS;
  profile->CopyFrom(profile_response.profile());
  return ::grpc::Status::OK;
}

}  // namespace recommendation_engine
