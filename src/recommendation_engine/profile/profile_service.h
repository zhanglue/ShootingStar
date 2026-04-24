#pragma once

#include <grpcpp/grpcpp.h>

#include <memory>

#include "protos/recommendation_engine/profile.grpc.pb.h"
#include "src/recommendation_engine/profile/profile_store.h"
#include "src/utilities/config_helper/config_helper.h"
#include "src/utilities/logger/logger.h"

namespace recommendation_engine {

class ProfileServiceImpl final : public ProfileService::Service {
 public:
  ProfileServiceImpl(
      ::shooting_star::utilities::YamlConfigHelper config,
      ::std::shared_ptr<const ::shooting_star::utilities::Logger> logger);

  ::grpc::Status GetProfile(::grpc::ServerContext* context,
                            const GetProfileRequest* request,
                            GetProfileResponse* response) override;

 private:
  ::shooting_star::utilities::YamlConfigHelper config_;
  ::std::shared_ptr<const ::shooting_star::utilities::Logger> logger_owner_;
  const ::shooting_star::utilities::Logger& logger_;
  ::std::unique_ptr<ProfileStore> profile_store_;
};

}  // namespace recommendation_engine
