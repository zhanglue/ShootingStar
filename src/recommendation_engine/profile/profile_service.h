#pragma once

#include <grpcpp/grpcpp.h>

#include "protos/recommendation_engine/profile.grpc.pb.h"
#include "src/recommendation_engine/profile/profile_store.h"
#include "src/utilities/global_config/global_config.h"

namespace recommendation_engine {

class ProfileServiceImpl final : public ProfileService::Service {
 public:
  explicit ProfileServiceImpl(
      const ::shooting_star::utilities::GlobalConfig& config =
          ::shooting_star::utilities::GlobalConfig::Get());

  ::grpc::Status GetProfile(::grpc::ServerContext* context,
                            const GetProfileRequest* request,
                            GetProfileResponse* response) override;
  ::grpc::Status BatchGetUserCfProfiles(
      ::grpc::ServerContext* context,
      const BatchGetUserCfProfilesRequest* request,
      BatchGetUserCfProfilesResponse* response) override;

 private:
  const ::shooting_star::utilities::GlobalConfig& config_;
  ::std::unique_ptr<ProfileStore> profile_store_;
};

}  // namespace recommendation_engine
