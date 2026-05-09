#pragma once

#include <memory>

#include <grpcpp/grpcpp.h>

#include "protos/recommendation_engine/profile.grpc.pb.h"
#include "src/recommendation_engine/profile/profile_store.h"
#include "src/utilities/global_config/global_config.h"

namespace shooting_star::recommendation_engine {

class ProfileServiceImpl final : public ProfileService::Service {
 public:
  explicit ProfileServiceImpl(::std::unique_ptr<ProfileStore> profile_store);

  static ::std::unique_ptr<ProfileServiceImpl> Create(
      const ::shooting_star::utilities::GlobalConfig& config);

  ::grpc::Status GetProfile(::grpc::ServerContext* context,
                            const GetProfileRequest* request,
                            GetProfileResponse* response) override;
  ::grpc::Status BatchGetUserCfProfiles(
      ::grpc::ServerContext* context,
      const BatchGetUserCfProfilesRequest* request,
      BatchGetUserCfProfilesResponse* response) override;

 private:
  ::std::unique_ptr<ProfileStore> profile_store_;
};

}  // namespace shooting_star::recommendation_engine
