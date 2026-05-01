#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "protos/recommendation_engine/profile.grpc.pb.h"
#include "src/recommendation_engine/retrieval/retrievers/user_cf/profile_store.h"

namespace recommendation_engine::user_cf {

class GrpcProfileStore final : public ProfileStore {
 public:
  explicit GrpcProfileStore(
      ::std::unique_ptr<ProfileService::StubInterface> profile_stub);

  ::std::optional<UserCfProfile> FindByUserId(uint64_t user_id) const override;
  ::std::vector<::std::optional<UserCfProfile>> FindByUserIds(
      const ::std::vector<uint64_t>& user_ids) const override;

 private:
  ::std::unique_ptr<ProfileService::StubInterface> profile_stub_;
};

}  // namespace recommendation_engine::user_cf
