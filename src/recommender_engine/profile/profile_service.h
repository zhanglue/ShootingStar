#pragma once

#include <grpcpp/grpcpp.h>

#include "protos/profile.grpc.pb.h"
#include "src/recommender_engine/profile/profile_store.h"

namespace recommender_engine {

class ProfileServiceImpl final : public ProfileService::Service {
 public:
  explicit ProfileServiceImpl(const ProfileStore* profile_store);

  ::grpc::Status GetProfile(::grpc::ServerContext* context,
                            const GetProfileRequest* request,
                            GetProfileResponse* response) override;

 private:
  const ProfileStore* profile_store_;
};

}  // namespace recommender_engine
