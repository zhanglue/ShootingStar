#pragma once

#include <grpcpp/grpcpp.h>

#include "protos/recommendation_engine/profile.grpc.pb.h"
#include "src/recommendation_engine/profile/profile_store.h"
#include "src/utilities/config_helper/config_helper.h"

namespace recommendation_engine {

class ProfileServiceImpl final : public ProfileService::Service {
 public:
  explicit ProfileServiceImpl(
      ::shooting_star::utilities::YamlConfigHelper config);

  ::grpc::Status GetProfile(::grpc::ServerContext* context,
                            const GetProfileRequest* request,
                            GetProfileResponse* response) override;

 private:
  ::shooting_star::utilities::YamlConfigHelper config_;
  ::std::unique_ptr<ProfileStore> profile_store_;
};

}  // namespace recommendation_engine
