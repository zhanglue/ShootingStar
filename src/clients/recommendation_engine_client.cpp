#include <getopt.h>
#include <grpcpp/grpcpp.h>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <chrono>

#include "protos/recommendation_engine/recommendation_engine.grpc.pb.h"
#include "src/clients/client_runtime.h"
#include "src/utilities/runtime_utilities/runtime_utilities.h"

using ::grpc::Channel;
using ::grpc::ClientContext;
using ::grpc::Status;
using ::recommendation_engine::RecommendRequest;
using ::recommendation_engine::RecommendResponse;
using ::shooting_star::utilities::GenerateGuid;
using ::std::chrono::duration_cast;
using ::std::chrono::steady_clock;
using ::std::cout;
using ::std::shared_ptr;
using ::std::string;
using ::std::unique_ptr;
using ::std::vector;

namespace recommendation_engine {
namespace {

class RecommendationEngineClient {
 public:
  explicit RecommendationEngineClient(shared_ptr<Channel> channel)
      : stub_(Gateway::NewStub(channel)) {}

  void Recommend(int user_id, int max_results) {
    RecommendRequest request;
    request.set_trace_id(GenerateGuid());
    request.set_request_id(GenerateGuid());
    request.set_user_id(user_id);
    request.set_max_results(max_results);

    RecommendResponse response;
    ClientContext context;

    const auto start = steady_clock::now();
    const Status status = stub_->Recommend(&context, request, &response);
    const auto elapsed_ms =
        duration_cast<::std::chrono::milliseconds>(steady_clock::now() - start)
            .count();
    cout << "Recommend RPC elapsed: " << elapsed_ms << " ms" << ::std::endl;

    if (status.ok()) {
      cout << ::std::endl;
      cout << "Recommend result: " << ::std::endl;
      cout << response.DebugString() << ::std::endl;
      cout << ::std::endl;
    } else {
      ::std::cerr << "RPC failed: " << status.error_code() << ", "
                  << status.error_message() << ::std::endl;
    }
  }

 private:
  unique_ptr<Gateway::Stub> stub_;
};

void PrintUsage() {
  cout << "Usage: recommendation_engine_client [options]\n"
       << "Options:\n"
       << "  -h, --help              Show this help message\n"
       << "  -i, --ip <IP>           Set server IP (default: 127.0.0.1)\n"
       << "  -p, --port <PORT>       Set server port (default: 50000)\n"
       << "  -u, --user-id <USER_ID> Add user ID to recommend (repeatable; "
          "default: {85566})\n"
       << "  -m, --max-results <N>   Set max results per request "
          "(default: 20)\n";
}

}  // namespace
}  // namespace recommendation_engine

int main(int argc, char** argv) {
  string ip = "127.0.0.1";
  string port = "50000";
  vector<int> user_ids = {};
  int max_results = 20;
  int default_user_id = 85566;

  struct option long_options[] = {
      {"help", no_argument, nullptr, 'h'},
      {"ip", required_argument, nullptr, 'i'},
      {"port", required_argument, nullptr, 'p'},
      {"user-id", required_argument, nullptr, 'u'},
      {"max-results", required_argument, nullptr, 'm'},
      {0, 0, 0, 0}};

  int opt;
  int option_index = 0;

  while ((opt = getopt_long(argc, argv, "hi:p:u:m:", long_options,
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
          user_ids.push_back(::std::stoi(optarg));
        } catch (const ::std::invalid_argument&) {
          ::std::cerr << "Error: user_id is not a valid integer: " << optarg
                      << "\n";
          return 1;
        } catch (const ::std::out_of_range&) {
          ::std::cerr << "Error: user_id is out of range: " << optarg << "\n";
          return 1;
        }
        break;
      case 'm':
        try {
          max_results = ::std::stoi(optarg);
        } catch (const ::std::invalid_argument&) {
          ::std::cerr << "Error: max_results is not a valid integer: " << optarg
                      << "\n";
          return 1;
        } catch (const ::std::out_of_range&) {
          ::std::cerr << "Error: max_results is out of range: " << optarg
                      << "\n";
          return 1;
        }
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
  cout << "Recommend for users:";
  for (int user_id : user_ids) {
    cout << " " << user_id;
  }
  cout << ::std::endl << ::std::endl;
  cout << "Requested max results: " << max_results << ::std::endl
       << ::std::endl;

  recommendation_engine::RecommendationEngineClient client(
      grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));
  for (int user_id : user_ids) {
    cout << "Recommend for user: " << user_id << ::std::endl;
    client.Recommend(user_id, max_results);
  }

  return 0;
}
