#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include <grpcpp/grpcpp.h>

namespace shooting_star {
namespace clients {

enum ClientExitCode {
  kOk = 0,
  kErr = 1,
  kArgs = 2,
};

std::string BuildTarget(std::string_view host, std::string_view port);

std::shared_ptr<grpc::Channel> CreateInsecureChannel(
    std::string_view target);

bool ParseIntArg(std::string_view text, std::string_view name, int* value);

bool ParseInt64Arg(std::string_view text, std::string_view name,
                   int64_t* value);

int64_t ElapsedMillisSince(
    std::chrono::steady_clock::time_point start_time);

void PrintRunStartedAtUtc();

void PrintTimestampUtc(std::string_view label);

void PrintRpcElapsed(std::string_view rpc_name, int64_t elapsed_ms);

void PrintRpcFailure(const grpc::Status& status);

}  // namespace clients
}  // namespace shooting_star
