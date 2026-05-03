#include <getopt.h>
#include <grpcpp/grpcpp.h>

#include <iostream>
#include <memory>

#include "protos/weather_forecast/fetcher.grpc.pb.h"
#include "src/clients/client_runtime.h"

using ::grpc::Channel;
using ::grpc::ClientContext;
using ::grpc::Status;
using ::std::cout;
using ::std::shared_ptr;
using ::std::string;
using ::std::unique_ptr;
using ::weather_flow::GetWeatherRequest;
using ::weather_flow::GetWeatherResponse;

namespace weather_flow {
namespace {

class FetcherClient {
 public:
  explicit FetcherClient(shared_ptr<Channel> channel)
      : stub_(Fetcher::NewStub(channel)) {}

  void GetWeather(const string& city) {
    GetWeatherRequest request;
    request.set_city(city);

    GetWeatherResponse response;
    ClientContext context;

    const Status status = stub_->GetWeather(&context, request, &response);

    if (status.ok()) {
      cout << "Weather in " << city << ": " << response.data().DebugString()
           << ::std::endl;
    } else {
      ::std::cerr << "RPC failed: " << status.error_code() << ", "
                  << status.error_message() << ::std::endl;
    }
  }

 private:
  unique_ptr<Fetcher::Stub> stub_;
};

void PrintUsage() {
  cout << "Usage: weather_client [options]\n"
       << "Options:\n"
       << "  -h, --help           Show this help message\n"
       << "  -i, --ip <IP>        Set server IP (default: 127.0.0.1)\n"
       << "  -p, --port <PORT>    Set server port (default: 40000)\n"
       << "  -c, --city <CITY>    Set city name for weather query (default: "
          "Denver)\n";
}

}  // namespace
}  // namespace weather_flow

int main(int argc, char** argv) {
  string ip = "127.0.0.1";
  string port = "40000";
  string city = "Denver";

  struct option long_options[] = {{"help", no_argument, nullptr, 'h'},
                                  {"ip", required_argument, nullptr, 'i'},
                                  {"port", required_argument, nullptr, 'p'},
                                  {"city", required_argument, nullptr, 'c'},
                                  {0, 0, 0, 0}};

  int opt;
  int option_index = 0;

  while ((opt = getopt_long(argc, argv, "hi:p:c:", long_options,
                            &option_index)) != -1) {
    switch (opt) {
      case 'h':
        weather_flow::PrintUsage();
        return 0;
      case 'i':
        ip = optarg;
        break;
      case 'p':
        port = optarg;
        break;
      case 'c':
        city = optarg;
        break;
      default:
        weather_flow::PrintUsage();
        return 1;
    }
  }

  const string target_str = ip + ":" + port;

  ::shooting_star::clients::PrintRunStartedAtUtc();
  cout << "Connecting to gRPC server at: " << target_str << ::std::endl;
  cout << "Fetching weather for city: " << city << ::std::endl << ::std::endl;

  weather_flow::FetcherClient client(
      grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));
  client.GetWeather(city);

  return 0;
}
