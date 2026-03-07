#include <getopt.h>
#include <iostream>
#include <memory>

#include <grpcpp/grpcpp.h>

#include "protos/weather_fetcher.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using std::cerr;
using std::cout;
using std::endl;
using std::string;
using weather_flow::WeatherFetcher;
using weather_flow::GetWeatherRequest;
using weather_flow::GetWeatherResponse;

class WeatherFetcherClient {
public:
    WeatherFetcherClient(std::shared_ptr<Channel> channel)
        : stub_(WeatherFetcher::NewStub(channel)) {}

    void GetWeather(const string& city) {
        GetWeatherRequest request;
        request.set_city(city);

        GetWeatherResponse response;
        ClientContext context;

        Status status = stub_->GetWeather(&context, request, &response);

        if (status.ok()) {
            cout << "Weather in " << city << ": " << response.data().DebugString() << endl;
        } else {
            cerr << "RPC failed: " << status.error_code() << ", " << status.error_message() << endl;
        }
    }

private:
    std::unique_ptr<WeatherFetcher::Stub> stub_;
};

void print_usage() {
    cout << "Usage: weather_client [options]\n"
              << "Options:\n"
              << "  -h, --help           Show this help message\n"
              << "  -i, --ip <IP>        Set server IP (default: localhost)\n"
              << "  -p, --port <PORT>    Set server port (default: 50051)\n"
              << "  -c, --city <CITY>    Set city name for weather query (default: Denver)\n";
}

int main(int argc, char** argv) {
    string ip = "localhost";
    string port = "50051";
    string city = "Denver";

    struct option long_options[] = {
        {"help",  no_argument,       0, 'h'},
        {"ip",    required_argument, 0, 'i'},
        {"port",  required_argument, 0, 'p'},
        {"city",  required_argument, 0, 'c'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "hi:p:c:", long_options, &option_index)) != -1) {
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
            case 'c':
                city = optarg;
                break;
            default:
                print_usage();
                return 1;
        }
    }

    string target_str = ip + ":" + port;

    cout << "Connecting to gRPC server at: " << target_str << endl;
    cout << "Fetching weather for city: " << city << endl << endl;

    WeatherFetcherClient client(grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));
    client.GetWeather(city);

    return 0;
}
