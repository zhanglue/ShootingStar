#include <getopt.h>
#include <grpcpp/grpcpp.h>

#include <cstdint>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#include "protos/recommendation_engine/retriever.grpc.pb.h"
#include "src/clients/client_runtime.h"
#include "src/utilities/runtime_utilities/runtime_utilities.h"
#include "src/utilities/local_profile_loader/local_profile_loader.h"

using ::grpc::Channel;
using ::grpc::ClientContext;
using ::grpc::Status;
using ::recommendation_engine::RetrieverRequest;
using ::recommendation_engine::RetrieverResponse;
using ::shooting_star::utilities::GenerateGuid;
using ::shooting_star::utilities::LoadProfileFromLocalFile;
using ::std::chrono::duration_cast;
using ::std::chrono::steady_clock;
using ::std::cout;
using ::std::shared_ptr;
using ::std::string;
using ::std::unique_ptr;
using ::std::vector;

constexpr char kDefaultProfileDataPath[] =
    "tests/testdata/recommendation_engine/local_recommendation_fixture/profiles.jsonl";

namespace recommendation_engine {
namespace {

class RetrieverClient {
 public:
  explicit RetrieverClient(shared_ptr<Channel> channel)
      : stub_(RetrieverService::NewStub(channel)) {}

  void Retrieve(int64_t user_id, int max_candidate_count,
                const string& profile_data_path,
                const string& executable_path) {
    RetrieverRequest request;
    request.set_trace_id(GenerateGuid());
    request.set_request_id(GenerateGuid());
    request.set_user_id(user_id);
    request.set_max_candidate_count(max_candidate_count);

    string error_msg;
    if (!LoadProfileFromLocalFile(profile_data_path, executable_path, user_id,
                                  request.mutable_profile(), &error_msg)) {
      ::std::cerr << "Failed to load profile: " << error_msg << ::std::endl;
      return;
    }

    cout << "Retrieve request:" << ::std::endl;
    cout << request.DebugString() << ::std::endl;

    RetrieverResponse response;
    ClientContext context;

    const auto start = steady_clock::now();
    const Status status = stub_->Retrieve(&context, request, &response);
    const auto elapsed_ms =
        duration_cast<::std::chrono::milliseconds>(steady_clock::now() - start)
            .count();
    cout << "Retrieve RPC elapsed: " << elapsed_ms << " ms" << ::std::endl;

    if (status.ok()) {
      cout << ::std::endl;
      cout << "Retrieve result:" << ::std::endl;
      cout << response.DebugString() << ::std::endl;
      cout << ::std::endl;
    } else {
      ::std::cerr << "RPC failed: " << status.error_code() << ", "
                  << status.error_message() << ::std::endl;
    }
  }

 private:
  unique_ptr<RetrieverService::Stub> stub_;
};

void PrintUsage() {
  cout << "Usage: retriever_client [options]\n"
       << "Options:\n"
       << "  -h, --help                        Show this help message\n"
       << "  -i, --ip <IP>                     Set server IP (default: "
          "127.0.0.1)\n"
       << "  -p, --port <PORT>                 Set server port (default: "
          "50210)\n"
       << "  -u, --user-id <USER_ID>           Add user ID to retrieve for "
          "(repeatable; default: {85566})\n"
       << "  -c, --max-candidate-count <COUNT> Set requested max candidate "
          "count (default: 20)\n"
       << "  -f, --profile-data-path <PATH>    Set demo profile jsonl path\n"
       << "                                    (default: "
       << kDefaultProfileDataPath << ")\n";
}

}  // namespace
}  // namespace recommendation_engine

int main(int argc, char** argv) {
  string ip = "127.0.0.1";
  string port = "50210";
  string profile_data_path = kDefaultProfileDataPath;
  vector<int64_t> user_ids = {};
  int64_t default_user_id = 85566;
  int max_candidate_count = 20;

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
        recommendation_engine::PrintUsage();
        return 0;
      case 'i':
        ip = optarg;
        break;
      case 'p':
        port = optarg;
        break;
      case 'u':
        try {
          user_ids.push_back(::std::stoll(optarg));
        } catch (const ::std::invalid_argument&) {
          ::std::cerr << "Error: user_id is not a valid integer: " << optarg
                      << "\n";
          return 1;
        } catch (const ::std::out_of_range&) {
          ::std::cerr << "Error: user_id is out of range: " << optarg << "\n";
          return 1;
        }
        break;
      case 'c':
        try {
          max_candidate_count = ::std::stoi(optarg);
        } catch (const ::std::invalid_argument&) {
          ::std::cerr << "Error: max_candidate_count is not a valid integer: "
                      << optarg << "\n";
          return 1;
        } catch (const ::std::out_of_range&) {
          ::std::cerr << "Error: max_candidate_count is out of range: "
                      << optarg << "\n";
          return 1;
        }
        break;
      case 'f':
        profile_data_path = optarg;
        break;
      default:
        recommendation_engine::PrintUsage();
        return 1;
    }
  }

  const string target_str = ip + ":" + port;
  if (user_ids.empty()) {
    user_ids.push_back(default_user_id);
  }

  ::shooting_star::clients::PrintRunStartedAtUtc();
  cout << "Connecting to gRPC server at: " << target_str << ::std::endl;
  cout << "Retrieving candidates for users:";
  for (int64_t user_id : user_ids) {
    cout << " " << user_id;
  }
  cout << ::std::endl;
  cout << "Using profile data from: " << profile_data_path << ::std::endl;
  cout << "Requested max candidate count: " << max_candidate_count
       << ::std::endl
       << ::std::endl;

  recommendation_engine::RetrieverClient client(
      grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));
  for (int64_t user_id : user_ids) {
    cout << "Retrieving candidates for user: " << user_id << ::std::endl;
    client.Retrieve(user_id, max_candidate_count, profile_data_path, argv[0]);
  }

  return 0;
}
