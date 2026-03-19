#include <getopt.h>
#include <iostream>
#include <memory>

#include <grpcpp/grpcpp.h>

#include "protos/recommendation_engine/recommendation_engine.grpc.pb.h"

using ::grpc::Channel;
using ::grpc::ClientContext;
using ::grpc::Status;
using ::std::cout;
using ::std::shared_ptr;
using ::std::string;
using ::std::unique_ptr;
using ::recommendation_engine::RecommendRequest;
using ::recommendation_engine::RecommendResponse;

namespace recommendation_engine {
namespace {

class RecommendationEngineClient {
 public:
  explicit RecommendationEngineClient(shared_ptr<Channel> channel)
      : stub_(Gateway::NewStub(channel)) {}

  void Recommend(int user_id) {
    RecommendRequest request;
    request.set_request_id("ABCDE-10155");
    request.set_user_id(user_id);

    RecommendResponse response;
    ClientContext context;

    const Status status = stub_->Recommend(&context, request, &response);

    if (status.ok()) {
      cout << ::std::endl;
      cout << "Recommend result: " << ::std::endl;
      cout << response.DebugString() << ::std::endl;
      cout << ::std::endl;
    } else {
      ::std::cerr << "RPC failed: " << status.error_code() << ", " << status.error_message()
                  << ::std::endl;
    }
  }

 private:
  unique_ptr<Gateway::Stub> stub_;
};

void PrintUsage() {
  cout << "Usage: recommendation_engine_client [options]\n"
       << "Options:\n"
       << "  -h, --help              Show this help message\n"
       << "  -i, --ip <IP>           Set server IP (default: localhost)\n"
       << "  -p, --port <PORT>       Set server port (default: 50000)\n"
       << "  -u, --user-id <USER_ID> Set user ID to recommend (default: 1001)\n";
}

}  // namespace
}  // namespace recommendation_engine

int main(int argc, char** argv) {
  string ip = "localhost";
  string port = "50100";
  int user_id = 1001;

  struct option long_options[] = {
      {"help", no_argument, nullptr, 'h'},
      {"ip", required_argument, nullptr, 'i'},
      {"port", required_argument, nullptr, 'p'},
      {"user_id", required_argument, nullptr, 'u'},
      {0, 0, 0, 0}};

  int opt;
  int option_index = 0;

  while ((opt = getopt_long(argc, argv, "hi:p:u:", long_options, &option_index)) != -1) {
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
          user_id = ::std::stoi(optarg);
        } catch (const ::std::invalid_argument&) {
          ::std::cerr << "Error: user_id is not a valid integer: " << optarg << "\n";
          return 1;
        } catch (const ::std::out_of_range&) {
          ::std::cerr << "Error: user_id is out of range: " << optarg << "\n";
          return 1;
        }
        break;
      default:
        recommendation_engine::PrintUsage();
        return 1;
    }
  }

  const string target_str = ip + ":" + port;

  cout << "Connecting to gRPC server at: " << target_str << ::std::endl;
  cout << "Recommend for user: " << user_id << ::std::endl << ::std::endl;

  recommendation_engine::RecommendationEngineClient client(
      grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));
  client.Recommend(user_id);

  return 0;
}
