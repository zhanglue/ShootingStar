#include <getopt.h>
#include <grpcpp/grpcpp.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <string>

#include "protos/weather_forecast/fetcher.grpc.pb.h"
#include "src/clients/common.h"

namespace {

using ::grpc::Channel;
using ::grpc::ClientContext;
using ::grpc::Status;
using ::shooting_star::clients::BuildTarget;
using ::shooting_star::clients::CreateInsecureChannel;
using ::shooting_star::clients::ElapsedMillisSince;
using ::shooting_star::clients::PrintRpcElapsed;
using ::shooting_star::clients::PrintRpcFailure;
using ::shooting_star::clients::PrintTimestampUtc;
using ::shooting_star::clients::ClientExitCode;
using ::shooting_star::clients::PrintRunStartedAtUtc;
using ::std::chrono::steady_clock;
using ::std::cout;
using ::std::endl;
using ::std::shared_ptr;
using ::std::string;
using ::std::unique_ptr;
using ::weather_flow::Fetcher;
using ::weather_flow::GetWeatherRequest;
using ::weather_flow::GetWeatherResponse;

struct Config {
  string ip = "127.0.0.1";
  string port = "40000";
  string city = "Denver";
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

bool ParseArgs(int argc, char** argv, Config* config) {
  struct option long_options[] = {
      {"help", no_argument, nullptr, 'h'},
      {"ip", required_argument, nullptr, 'i'},
      {"port", required_argument, nullptr, 'p'},
      {"city", required_argument, nullptr, 'c'},
      {0, 0, 0, 0},
  };

  int opt;
  int option_index = 0;
  while ((opt = getopt_long(argc, argv, "hi:p:c:", long_options,
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
      case 'c':
        config->city = optarg;
        break;
      default:
        PrintUsage();
        return false;
    }
  }
  return true;
}

void PrintConfig(const Config& config) {
  const string target = BuildTarget(config.ip, config.port);
  cout << "Client config:" << endl;
  cout << "  ip: " << config.ip << endl;
  cout << "  port: " << config.port << endl;
  cout << "  target: " << target << endl;
  cout << "  city: " << config.city << endl;
  cout << endl;
}

class FetcherClient {
 public:
  static FetcherClient Create(const Config& config) {
    const string target = BuildTarget(config.ip, config.port);
    shared_ptr<Channel> channel = CreateInsecureChannel(target);
    return FetcherClient(channel);
  }

  explicit FetcherClient(shared_ptr<Channel> channel)
      : stub_(Fetcher::NewStub(channel)) {}

  bool launch_request(const string& city) {
    GetWeatherRequest request;
    request.set_city(city);

    cout << "GetWeather request:" << endl;
    cout << request.DebugString() << endl;

    GetWeatherResponse response;
    ClientContext context;

    PrintTimestampUtc("GetWeather request started at UTC");
    const auto start = steady_clock::now();
    const Status status = stub_->GetWeather(&context, request, &response);
    PrintTimestampUtc("GetWeather response received at UTC");
    PrintRpcElapsed("GetWeather", ElapsedMillisSince(start));

    cout << "GetWeather response:" << endl;
    cout << response.DebugString() << endl;
    if (!status.ok()) {
      PrintRpcFailure(status);
      return false;
    }
    return true;
  }

 private:
  unique_ptr<Fetcher::Stub> stub_;
};

}  // namespace

int main(int argc, char** argv) {
  Config config;
  if (!ParseArgs(argc, argv, &config)) {
    return ClientExitCode::kArgs;
  }

  PrintRunStartedAtUtc();
  PrintConfig(config);

  FetcherClient client = FetcherClient::Create(config);
  if (!client.launch_request(config.city)) {
    return ClientExitCode::kErr;
  }
  return ClientExitCode::kOk;
}
