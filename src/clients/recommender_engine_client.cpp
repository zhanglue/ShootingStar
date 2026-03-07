#include <getopt.h>
#include <iostream>
#include <memory>

#include <grpcpp/grpcpp.h>

#include "protos/recommender_engine.grpc.pb.h"

using ::grpc::Channel;
using ::grpc::ClientContext;
using ::grpc::Status;
using ::std::cerr;
using ::std::cout;
using ::std::endl;
using ::std::string;
using recommender_engine::Gateway;
using recommender_engine::RecommendRequest;
using recommender_engine::RecommendResponse;

class RecommenderEngineClient {
public:
    RecommenderEngineClient(std::shared_ptr<Channel> channel)
        : stub_(Gateway::NewStub(channel)) {}

    void Recommend(int user_id) {
        RecommendRequest request;
        request.set_request_id("ABCDE-10155");
        request.set_user_id(user_id);

        RecommendResponse response;
        ClientContext context;

        Status status = stub_->Recommend(&context, request, &response);

        if (status.ok()) {
          cout << endl;
          cout << "Recommend result: " << endl;
          cout << response.DebugString() << endl;
          cout << endl;
        } else {
          cerr << "RPC failed: " << status.error_code() << ", " << status.error_message() << endl;
        }
    }

private:
    std::unique_ptr<Gateway::Stub> stub_;
};

void print_usage() {
    cout << "Usage: recommender_engine_client [options]\n"
              << "Options:\n"
              << "  -h, --help              Show this help message\n"
              << "  -i, --ip <IP>           Set server IP (default: localhost)\n"
              << "  -p, --port <PORT>       Set server port (default: 50051)\n"
              << "  -u, --user-id <USER_ID> Set user ID to recommed (default: 1001)\n";
}

int main(int argc, char** argv) {
    string ip = "localhost";
    string port = "50051";
    int user_id = 1001;

    struct option long_options[] = {
        {"help",    no_argument,       0, 'h'},
        {"ip",      required_argument, 0, 'i'},
        {"port",    required_argument, 0, 'p'},
        {"user_id", required_argument, 0, 'u'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "hi:p:u:", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'h':
                print_usage();
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
                } catch (const ::std::invalid_argument& e) {
                    std::cerr << "Error: user_id is not a valid integer: " << optarg << "\n";
                    return 1;
                } catch (const ::std::out_of_range& e) {
                    std::cerr << "Error: user_id is out of range: " << optarg << "\n";
                    return 1;
                }
                break;
            default:
                print_usage();
                return 1;
        }
    }

    string target_str = ip + ":" + port;

    cout << "Connecting to gRPC server at: " << target_str << endl;
    cout << "Recommend for user: " << user_id << endl << endl;

    RecommenderEngineClient client(grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));
    client.Recommend(user_id);

    return 0;
}
