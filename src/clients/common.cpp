#include "src/clients/common.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

#include <grpcpp/grpcpp.h>

namespace shooting_star {
namespace clients {

using grpc::Channel;
using grpc::CreateChannel;
using grpc::InsecureChannelCredentials;
using grpc::Status;
using std::chrono::duration_cast;
using std::chrono::milliseconds;
using std::chrono::steady_clock;
using std::chrono::system_clock;
using std::endl;
using std::shared_ptr;
using std::string;
using std::string_view;

string BuildTarget(string_view host, string_view port) {
  string target(host);
  target.push_back(':');
  target.append(port);
  return target;
}

shared_ptr<Channel> CreateInsecureChannel(string_view target) {
  return CreateChannel(string(target), InsecureChannelCredentials());
}

bool ParseIntArg(string_view text, string_view name, int* value) {
  try {
    *value = std::stoi(string(text));
  } catch (const std::invalid_argument&) {
    std::cout << "Error: " << name << " is not a valid integer: " << text
                << "\n";
    return false;
  } catch (const std::out_of_range&) {
    std::cout << "Error: " << name << " is out of range: " << text << "\n";
    return false;
  }
  return true;
}

bool ParseInt64Arg(string_view text, string_view name, int64_t* value) {
  try {
    *value = std::stoll(string(text));
  } catch (const std::invalid_argument&) {
    std::cout << "Error: " << name << " is not a valid integer: " << text
                << "\n";
    return false;
  } catch (const std::out_of_range&) {
    std::cout << "Error: " << name << " is out of range: " << text << "\n";
    return false;
  }
  return true;
}

int64_t ElapsedMillisSince(steady_clock::time_point start_time) {
  return duration_cast<milliseconds>(steady_clock::now() - start_time).count();
}

void PrintRunStartedAtUtc() {
  PrintTimestampUtc("Run started at UTC");
}

void PrintTimestampUtc(string_view label) {
  const system_clock::time_point now = system_clock::now();
  const std::time_t now_time = system_clock::to_time_t(now);
  std::cout << label << ": "
              << std::put_time(std::gmtime(&now_time), "%Y-%m-%dT%H:%M:%SZ")
              << "\n";
}

void PrintRpcElapsed(string_view rpc_name, int64_t elapsed_ms) {
  std::cout << rpc_name << " RPC elapsed: " << elapsed_ms << " ms"
              << std::endl;
}

void PrintRpcFailure(const Status& status) {
  std::cout << "RPC failed: " << status.error_code() << ", "
              << status.error_message() << std::endl;
}

}  // namespace clients
}  // namespace shooting_star
