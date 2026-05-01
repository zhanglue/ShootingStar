#include "src/recommendation_engine/retrieval/retrievers/user_cf/grpc_profile_store.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace recommendation_engine::user_cf {

using ::grpc::ClientContext;
using ::grpc::Status;
using ::std::format;
using ::std::optional;
using ::std::runtime_error;
using ::std::size_t;
using ::std::unique_ptr;
using ::std::vector;

GrpcProfileStore::GrpcProfileStore(
    unique_ptr<ProfileService::StubInterface> profile_stub)
    : profile_stub_(::std::move(profile_stub)) {
  if (profile_stub_ == nullptr) {
    throw ::std::invalid_argument("GrpcProfileStore profile_stub must not be null");
  }
}

optional<UserCfProfile> GrpcProfileStore::FindByUserId(uint64_t user_id) const {
  const vector<optional<UserCfProfile>> profiles = FindByUserIds({user_id});
  if (profiles.empty()) {
    return ::std::nullopt;
  }
  return profiles.front();
}

vector<optional<UserCfProfile>> GrpcProfileStore::FindByUserIds(
    const vector<uint64_t>& user_ids) const {
  vector<optional<UserCfProfile>> profiles(user_ids.size());
  if (user_ids.empty()) {
    return profiles;
  }

  BatchGetUserCfProfilesRequest request;
  request.set_request_id("user_cf_neighbor_batch");
  vector<size_t> valid_indexes;
  valid_indexes.reserve(user_ids.size());
  for (size_t i = 0; i < user_ids.size(); ++i) {
    if (user_ids[i] == 0 ||
        user_ids[i] > static_cast<uint64_t>(::std::numeric_limits<int64_t>::max())) {
      continue;
    }
    request.add_user_ids(static_cast<int64_t>(user_ids[i]));
    valid_indexes.emplace_back(i);
  }

  if (valid_indexes.empty()) {
    return profiles;
  }

  BatchGetUserCfProfilesResponse response;
  ClientContext context;
  const Status status =
      profile_stub_->BatchGetUserCfProfiles(&context, request, &response);
  if (!status.ok()) {
    throw runtime_error(format("Failed to fetch user_cf profiles: {}",
                               status.error_message()));
  }

  if (response.status() != ProfileServiceStatus::PROFILE_SUCCESS) {
    throw runtime_error(format("Profile service returned status {} for user_cf profile batch.",
                               static_cast<int>(response.status())));
  }
  if (response.results_size() != static_cast<int>(valid_indexes.size())) {
    throw runtime_error(
        "Profile service user_cf batch returned mismatched result size");
  }

  for (int i = 0; i < response.results_size(); ++i) {
    const UserCfProfileResult& result = response.results(i);
    if (result.status() != ProfileServiceStatus::PROFILE_SUCCESS) {
      continue;
    }
    profiles[valid_indexes[static_cast<size_t>(i)]] = result.profile();
  }
  return profiles;
}

}  // namespace recommendation_engine::user_cf
