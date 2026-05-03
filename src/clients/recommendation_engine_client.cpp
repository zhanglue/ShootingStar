#include <getopt.h>
#include <grpcpp/grpcpp.h>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "protos/recommendation_engine/recommendation_engine.grpc.pb.h"
#include "src/clients/client_runtime.h"

using ::grpc::Channel;
using ::grpc::ClientContext;
using ::grpc::Status;
using ::recommendation_engine::RecommendRequest;
using ::recommendation_engine::RecommendResponse;
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

  void Recommend(int user_id, int recommendation_results_count) {
    RecommendRequest request;
    request.set_request_id("ABCDE-10155");
    request.set_user_id(user_id);
    request.set_recommendation_results_count(recommendation_results_count);

    RecommendResponse response;
    ClientContext context;

    const Status status = stub_->Recommend(&context, request, &response);

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
          "default: {1001})\n"
       << "  -m, --recommendation-results <N> Set recommendation results count "
          "(default: 20)\n"
       << "  --max-candidates <N> Legacy alias for "
          "--recommendation-results\n";
}

}  // namespace
}  // namespace recommendation_engine

int main(int argc, char** argv) {
  string ip = "127.0.0.1";
  string port = "50000";
  vector<int> user_ids = {};
  int recommendation_results_count = 20;
  int default_user_id = 1001;

  struct option long_options[] = {
      {"help", no_argument, nullptr, 'h'},
      {"ip", required_argument, nullptr, 'i'},
      {"port", required_argument, nullptr, 'p'},
      {"user-id", required_argument, nullptr, 'u'},
      {"recommendation-results", required_argument, nullptr, 'm'},
      {"max-candidates", required_argument, nullptr, 'm'},
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
          recommendation_results_count = ::std::stoi(optarg);
        } catch (const ::std::invalid_argument&) {
          ::std::cerr
              << "Error: recommendation_results_count is not a valid integer: "
              << optarg << "\n";
          return 1;
        } catch (const ::std::out_of_range&) {
          ::std::cerr << "Error: recommendation_results_count is out of range: "
                      << optarg << "\n";
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
  cout << "Requested recommendation results count: "
       << recommendation_results_count << ::std::endl
       << ::std::endl;

  recommendation_engine::RecommendationEngineClient client(
      grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));
  for (int user_id : user_ids) {
    cout << "Recommend for user: " << user_id << ::std::endl;
    client.Recommend(user_id, recommendation_results_count);
  }

  return 0;
}
