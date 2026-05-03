#pragma once

#include <chrono>
#include <string>
#include <string_view>

#include <grpcpp/grpcpp.h>

namespace shooting_star {
namespace utilities {

enum class RpcDeadlineStatus {
  kOk,
  kCancelled,
  kClientDeadlineExceeded,
  kServerDeadlineExceeded,
};

RpcDeadlineStatus CheckGrpcServerDeadline(
    const ::grpc::ServerContext* context,
    ::std::chrono::steady_clock::time_point server_deadline);

::std::string Base64Encode(::std::string_view input);

::std::string GetEnvOrDefault(::std::string_view name,
                              ::std::string default_value);

bool GetEnvFlagOrDefault(::std::string_view name, bool default_value);

::std::string ResolveWorkspaceRelativePath(
    const ::std::string& path,
    const ::std::string& executable_path = "");

void ValidateTimeoutNotGreater(
    ::std::string_view inner_key,
    ::std::chrono::milliseconds inner,
    ::std::string_view outer_key,
    ::std::chrono::milliseconds outer);

void ValidateTimeoutSumNotGreater(
    ::std::string_view first_inner_key,
    ::std::chrono::milliseconds first_inner,
    ::std::string_view second_inner_key,
    ::std::chrono::milliseconds second_inner,
    ::std::string_view outer_key,
    ::std::chrono::milliseconds outer);

void TrimLeadingSlashes(::std::string& value);

void TrimTrailingSlashes(::std::string& value);

void TrimWhitespace(::std::string_view& value);

::std::string GenerateGuid();

}  // namespace utilities
}  // namespace shooting_star
