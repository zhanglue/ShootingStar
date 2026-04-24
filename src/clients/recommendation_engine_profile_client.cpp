#include <getopt.h>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "protos/recommendation_engine/profile.grpc.pb.h"
#include "src/clients/client_runtime.h"

using ::grpc::Channel;
using ::grpc::ClientContext;
using ::grpc::Status;
using ::std::cout;
using ::std::shared_ptr;
using ::std::string;
using ::std::unique_ptr;
using ::recommendation_engine::GetProfileRequest;
using ::recommendation_engine::GetProfileResponse;

namespace recommendation_engine {
namespace {

class ProfileClient {
 public:
  explicit ProfileClient(shared_ptr<Channel> channel)
      : stub_(ProfileService::NewStub(channel)) {}

  void GetProfile(int user_id) {
    GetProfileRequest request;
    request.set_request_id("ABCDE-10156");
    request.set_user_id(user_id);

    GetProfileResponse response;
    ClientContext context;

    const Status status = stub_->GetProfile(&context, request, &response);

    if (status.ok()) {
      cout << ::std::endl;
      cout << "Profile result:" << ::std::endl;
      cout << response.DebugString() << ::std::endl;
      cout << ::std::endl;
    } else {
      ::std::cerr << "RPC failed: " << status.error_code() << ", " << status.error_message()
                  << ::std::endl;
    }
  }

 private:
  unique_ptr<ProfileService::Stub> stub_;
};

void PrintUsage() {
  cout << "Usage: profile_client [options]\n"
       << "Options:\n"
       << "  -h, --help              Show this help message\n"
       << "  -i, --ip <IP>           Set server IP (default: localhost)\n"
       << "  -p, --port <PORT>       Set server port (default: 50100)\n"
       << "  -u, --user-id <USER_ID> Add user ID to query (repeatable; default: 10, 120, 2300)\n";
}

}  // namespace
}  // namespace recommendation_engine

int main(int argc, char** argv) {
  string ip = "localhost";
  string port = "50100";
  ::std::vector<int> user_ids = {10, 120, 2300};
  bool has_custom_user_ids = false;

  struct option long_options[] = {
      {"help", no_argument, nullptr, 'h'},
      {"ip", required_argument, nullptr, 'i'},
      {"port", required_argument, nullptr, 'p'},
      {"user-id", required_argument, nullptr, 'u'},
      {0, 0, 0, 0},
  };

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
          if (!has_custom_user_ids) {
            user_ids.clear();
            has_custom_user_ids = true;
          }
          user_ids.push_back(::std::stoi(optarg));
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

  ::shooting_star::clients::PrintRunStartedAtUtc();
  cout << "Connecting to gRPC server at: " << target_str << ::std::endl;
  cout << "Fetching profiles for users:";
  for (int user_id : user_ids) {
    cout << " " << user_id;
  }
  cout << ::std::endl << ::std::endl;

  recommendation_engine::ProfileClient client(
      grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));
  for (int user_id : user_ids) {
    cout << "Fetching profile for user: " << user_id << ::std::endl;
    client.GetProfile(user_id);
  }

  return 0;
}
