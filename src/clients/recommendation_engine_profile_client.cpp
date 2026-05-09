#include <getopt.h>
#include <grpcpp/grpcpp.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "protos/recommendation_engine/profile.grpc.pb.h"
#include "src/clients/common.h"
#include "src/utilities/runtime_utilities/runtime_utilities.h"

namespace {

using ::grpc::Channel;
using ::grpc::ClientContext;
using ::grpc::Status;
using ::recommendation_engine::GetProfileRequest;
using ::recommendation_engine::GetProfileResponse;
using ::recommendation_engine::ProfileService;
using ::shooting_star::clients::BuildTarget;
using ::shooting_star::clients::CreateInsecureChannel;
using ::shooting_star::clients::ElapsedMillisSince;
using ::shooting_star::clients::ParseIntArg;
using ::shooting_star::clients::PrintRpcElapsed;
using ::shooting_star::clients::PrintRpcFailure;
using ::shooting_star::clients::PrintTimestampUtc;
using ::shooting_star::clients::ClientExitCode;
using ::shooting_star::clients::PrintRunStartedAtUtc;
using ::shooting_star::utilities::GenerateGuid;
using ::std::chrono::steady_clock;
using ::std::cout;
using ::std::endl;
using ::std::shared_ptr;
using ::std::string;
using ::std::unique_ptr;
using ::std::vector;

constexpr int kDefaultUserId = 85566;

struct Config {
  string ip = "127.0.0.1";
  string port = "50100";
  vector<int> user_ids;
};

void PrintUsage() {
  cout << "Usage: profile_client [options]\n"
       << "Options:\n"
       << "  -h, --help              Show this help message\n"
       << "  -i, --ip <IP>           Set server IP (default: 127.0.0.1)\n"
       << "  -p, --port <PORT>       Set server port (default: 50100)\n"
       << "  -u, --user-id <USER_ID> Add user ID to query (repeatable; "
          "default: {85566})\n";
}

bool ParseArgs(int argc, char** argv, Config* config) {
  struct option long_options[] = {
      {"help", no_argument, nullptr, 'h'},
      {"ip", required_argument, nullptr, 'i'},
      {"port", required_argument, nullptr, 'p'},
      {"user-id", required_argument, nullptr, 'u'},
      {0, 0, 0, 0},
  };

  int opt;
  int option_index = 0;
  while ((opt = getopt_long(argc, argv, "hi:p:u:", long_options,
                            &option_index)) != -1) {
    switch (opt) {
      case 'h':
        PrintUsage();
        return false;
      case 'i':
        config->ip = optarg;
        break;
      case 'p':
        config->port = optarg;
        break;
      case 'u': {
        int user_id = 0;
        if (!ParseIntArg(optarg, "user_id", &user_id)) {
          return false;
        }
        config->user_ids.push_back(user_id);
        break;
      }
      default:
        PrintUsage();
        return false;
    }
  }
  if (config->user_ids.empty()) {
    config->user_ids.push_back(kDefaultUserId);
  }
  return true;
}

void PrintConfig(const Config& config) {
  const string target = BuildTarget(config.ip, config.port);
  cout << "Client config:" << endl;
  cout << "  ip: " << config.ip << endl;
  cout << "  port: " << config.port << endl;
  cout << "  target: " << target << endl;
  cout << "  user_ids:";
  for (int user_id : config.user_ids) {
    cout << " " << user_id;
  }
  cout << endl << endl;
}

class ProfileClient {
 public:
  static ProfileClient Create(const Config& config) {
    const string target = BuildTarget(config.ip, config.port);
    shared_ptr<Channel> channel =
        CreateInsecureChannel(target);
    return ProfileClient(channel);
  }

  explicit ProfileClient(shared_ptr<Channel> channel)
      : stub_(ProfileService::NewStub(channel)) {}

  bool launch_request(int user_id) {
    GetProfileRequest request;
    request.set_trace_id(GenerateGuid());
    request.set_request_id(GenerateGuid());
    request.set_user_id(user_id);

    cout << "GetProfile request:" << endl;
    cout << request.DebugString() << endl;

    GetProfileResponse response;
    ClientContext context;

    PrintTimestampUtc("GetProfile request started at UTC");
    const auto start = steady_clock::now();
    const Status status = stub_->GetProfile(&context, request, &response);
    PrintTimestampUtc("GetProfile response received at UTC");
    PrintRpcElapsed("GetProfile", ElapsedMillisSince(start));

    cout << "GetProfile response:" << endl;
    cout << response.DebugString() << endl;
    if (!status.ok()) {
      PrintRpcFailure(status);
      return false;
    }
    return true;
  }

 private:
  unique_ptr<ProfileService::Stub> stub_;
};

}  // namespace

int main(int argc, char** argv) {
  Config config;
  if (!ParseArgs(argc, argv, &config)) {
    return ClientExitCode::kArgs;
  }

  PrintRunStartedAtUtc();
  PrintConfig(config);

  ProfileClient client = ProfileClient::Create(config);
  for (int user_id : config.user_ids) {
    cout << "Launching GetProfile for user: " << user_id << endl;
    if (!client.launch_request(user_id)) {
      return ClientExitCode::kErr;
    }
  }
  return ClientExitCode::kOk;
}
