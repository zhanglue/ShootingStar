#include <getopt.h>
#include <grpcpp/grpcpp.h>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "protos/recommendation_engine/retrieval.grpc.pb.h"
#include "src/clients/common.h"
#include "src/utilities/local_profile_loader/local_profile_loader.h"
#include "src/utilities/runtime_utilities/runtime_utilities.h"

namespace {

using ::grpc::Channel;
using ::grpc::ClientContext;
using ::grpc::Status;
using ::recommendation_engine::RetrieveRequest;
using ::recommendation_engine::RetrieveResponse;
using ::recommendation_engine::RetrievalService;
using ::shooting_star::clients::BuildTarget;
using ::shooting_star::clients::CreateInsecureChannel;
using ::shooting_star::clients::ElapsedMillisSince;
using ::shooting_star::clients::ParseInt64Arg;
using ::shooting_star::clients::ParseIntArg;
using ::shooting_star::clients::PrintRpcElapsed;
using ::shooting_star::clients::PrintRpcFailure;
using ::shooting_star::clients::PrintTimestampUtc;
using ::shooting_star::clients::ClientExitCode;
using ::shooting_star::clients::PrintRunStartedAtUtc;
using ::shooting_star::utilities::GenerateGuid;
using ::shooting_star::utilities::LoadProfileFromLocalFile;
using ::shooting_star::utilities::ResolveWorkspaceRelativePath;
using ::std::chrono::steady_clock;
using ::std::cout;
using ::std::endl;
using ::std::shared_ptr;
using ::std::string;
using ::std::unique_ptr;
using ::std::vector;

constexpr char kDefaultProfileDataPath[] =
    "tests/testdata/recommendation_engine/local_recommendation_fixture/profiles.jsonl";
constexpr int64_t kDefaultUserId = 85566;

struct Config {
  string ip = "127.0.0.1";
  string port = "50200";
  string profile_data_path = kDefaultProfileDataPath;
  vector<int64_t> user_ids;
  int max_candidate_count = 20;
};

void PrintUsage() {
  cout << "Usage: retrieval_client [options]\n"
       << "Options:\n"
       << "  -h, --help                        Show this help message\n"
       << "  -i, --ip <IP>                     Set server IP (default: "
          "127.0.0.1)\n"
       << "  -p, --port <PORT>                 Set server port (default: "
          "50200)\n"
       << "  -u, --user-id <USER_ID>           Add user ID to retrieve for "
          "(repeatable; default: {85566})\n"
       << "  -c, --max-candidate-count <COUNT> Set requested max candidate "
          "count (default: 20)\n"
       << "  -f, --profile-data-path <PATH>    Set demo profile jsonl path\n"
       << "                                    (default: "
       << kDefaultProfileDataPath << ")\n";
}

bool ParseArgs(int argc, char** argv, Config* config) {
  struct option long_options[] = {
      {"help", no_argument, nullptr, 'h'},
      {"ip", required_argument, nullptr, 'i'},
      {"port", required_argument, nullptr, 'p'},
      {"user-id", required_argument, nullptr, 'u'},
      {"max-candidate-count", required_argument, nullptr, 'c'},
      {"profile-data-path", required_argument, nullptr, 'f'},
      {0, 0, 0, 0},
  };

  int opt;
  int option_index = 0;
  while ((opt = getopt_long(argc, argv, "hi:p:u:c:f:", long_options,
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
        int64_t user_id = 0;
        if (!ParseInt64Arg(optarg, "user_id", &user_id)) {
          return false;
        }
        config->user_ids.push_back(user_id);
        break;
      }
      case 'c':
        if (!ParseIntArg(optarg, "max_candidate_count",
                         &config->max_candidate_count)) {
          return false;
        }
        break;
      case 'f':
        config->profile_data_path = optarg;
        break;
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
  for (int64_t user_id : config.user_ids) {
    cout << " " << user_id;
  }
  cout << endl;
  cout << "  max_candidate_count: " << config.max_candidate_count
       << endl;
  cout << "  profile_data_path: " << config.profile_data_path << endl;
  cout << endl;
}

class RetrievalClient {
 public:
  static RetrievalClient Create(const Config& config) {
    const string target = BuildTarget(config.ip, config.port);
    shared_ptr<Channel> channel =
        CreateInsecureChannel(target);
    return RetrievalClient(channel);
  }

  explicit RetrievalClient(shared_ptr<Channel> channel)
      : stub_(RetrievalService::NewStub(channel)) {}

  bool launch_request(int64_t user_id,
                      int max_candidate_count,
                      const string& profile_data_path,
                      const string& executable_path) {
    RetrieveRequest request;
    request.set_trace_id(GenerateGuid());
    request.set_request_id(GenerateGuid());
    request.set_user_id(user_id);
    request.set_max_candidate_count(max_candidate_count);

    string error_msg;
    const string resolved_profile_data_path =
        ResolveWorkspaceRelativePath(profile_data_path, executable_path);
    if (!LoadProfileFromLocalFile(resolved_profile_data_path, user_id,
                                  request.mutable_profile(), &error_msg)) {
      cout << "Failed to load profile: " << error_msg << endl;
      return false;
    }

    cout << "Retrieve request:" << endl;
    cout << request.DebugString() << endl;

    RetrieveResponse response;
    ClientContext context;

    PrintTimestampUtc("Retrieve request started at UTC");
    const auto start = steady_clock::now();
    const Status status = stub_->Retrieve(&context, request, &response);
    PrintTimestampUtc("Retrieve response received at UTC");
    PrintRpcElapsed("Retrieve", ElapsedMillisSince(start));

    cout << "Retrieve response:" << endl;
    cout << response.DebugString() << endl;
    if (!status.ok()) {
      PrintRpcFailure(status);
      return false;
    }
    return true;
  }

 private:
  unique_ptr<RetrievalService::Stub> stub_;
};

}  // namespace

int main(int argc, char** argv) {
  Config config;
  if (!ParseArgs(argc, argv, &config)) {
    return ClientExitCode::kArgs;
  }

  PrintRunStartedAtUtc();
  PrintConfig(config);

  RetrievalClient client = RetrievalClient::Create(config);
  for (int64_t user_id : config.user_ids) {
    cout << "Launching Retrieve for user: " << user_id << endl;
    if (!client.launch_request(user_id, config.max_candidate_count,
                               config.profile_data_path, argv[0])) {
      return ClientExitCode::kErr;
    }
  }
  return ClientExitCode::kOk;
}
