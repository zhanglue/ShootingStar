#include "src/recommendation_engine/profile/profile_service.h"

#include <format>

namespace recommendation_engine {

using ::grpc::ServerContext;
using ::grpc::Status;
using ::grpc::StatusCode;
using ::std::format;

ProfileServiceImpl::ProfileServiceImpl(const ProfileStore* profile_store)
    : profile_store_(profile_store) {}

Status ProfileServiceImpl::GetProfile(ServerContext* context,
                                      const GetProfileRequest* request,
                                      GetProfileResponse* response) {
  (void)context;

  response->mutable_request()->CopyFrom(*request);

  if (profile_store_ == nullptr) {
    response->set_status(ProfileServiceStatus::PROFILE_SYSTEM_ERROR);
    return Status(StatusCode::INTERNAL, "Profile store is not initialized.");
  }

  const Profile* profile = profile_store_->FindByUserId(request->user_id());
  if (profile == nullptr) {
    response->set_status(ProfileServiceStatus::PROFILE_USER_NOT_FOUND);
    return Status(
        StatusCode::NOT_FOUND,
        format("User ID of {} not found.", request->user_id()));
  }

  response->set_status(ProfileServiceStatus::PROFILE_SUCCESS);
  response->mutable_profile()->CopyFrom(*profile);
  return Status::OK;
}

}  // namespace recommendation_engine
