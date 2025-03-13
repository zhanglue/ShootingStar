#include <iostream>
#include <memory>
#include <grpcpp/grpcpp.h>

#include "protos/weather_fetcher.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using weather_flow::WeatherFetcher;
using weather_flow::GetWeatherRequest;
using weather_flow::GetWeatherResponse;

class WeatherFetcherClient {
public:
    WeatherFetcherClient(std::shared_ptr<Channel> channel)
        : stub_(WeatherFetcher::NewStub(channel)) {}

    void GetWeather(const std::string& city) {
        GetWeatherRequest request;
        request.set_city(city);

        GetWeatherResponse response;
        ClientContext context;

        Status status = stub_->GetWeather(&context, request, &response);

        if (status.ok()) {
            std::cout << "Weather in " << city << ": " << response.data().DebugString() << std::endl;
        } else {
            std::cerr << "RPC failed: " << status.error_code() << ", " << status.error_message() << std::endl;
        }
    }

private:
    std::unique_ptr<WeatherFetcher::Stub> stub_;
};

int main(int argc, char** argv) {
    std::string target_str = "localhost:50051";
    if (argc > 1) {
        target_str = argv[1];
    }

    WeatherFetcherClient client(grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));
    std::string city = "Denver";
    if (argc > 2) {
        city = argv[2];
    }

    client.GetWeather(city);

    return 0;
}
