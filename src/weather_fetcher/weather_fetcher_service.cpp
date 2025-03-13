#include "src/weather_fetcher/weather_fetcher_service.h"

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <iostream>
#include <memory>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_format.h"

#include "protos/weather_fetcher.grpc.pb.h"

namespace weather_flow {

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::StatusCode;
using std::cout;
using std::endl;
using std::string;
using weather_flow::WeatherFetcher;
using weather_flow::GetWeatherRequest;
using weather_flow::GetWeatherResponse;

// Hardcoded weather data for cities.
WeatherFetcherServiceImpl::WeatherFetcherServiceImpl() {
  weather_data_map_["San Francisco"] = WeatherData::SUNNY;
  weather_data_map_["New York"] = WeatherData::CLOUDY;
  weather_data_map_["Seattle"] = WeatherData::RAINY;
  weather_data_map_["Denver"] = WeatherData::SNOWY;
}

Status WeatherFetcherServiceImpl::GetWeather(
    ServerContext* context,
    const GetWeatherRequest* request,
    GetWeatherResponse* response) {

  string city = request->city();
  auto it = weather_data_map_.find(city);
  if (it == weather_data_map_.end()) {
    return Status(StatusCode::NOT_FOUND, "City not found.");
  }

  WeatherData* weather_data = response->mutable_data();
  weather_data->set_condition(it->second);
  weather_data->set_temperature(22.0f);

  cout << "Requested weather for city: " << city << endl;

  return Status::OK;
}

}  // namespace weather_flow
