#include "src/weather_forecast/fetcher/fetcher_service.h"

#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_format.h"

#include "protos/weather_forecast/fetcher.grpc.pb.h"

namespace weather_flow {

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::StatusCode;
using std::cout;
using std::endl;
using std::string;
using weather_flow::Fetcher;
using weather_flow::GetWeatherRequest;
using weather_flow::GetWeatherResponse;

// Hardcoded weather data for cities.
FetcherServiceImpl::FetcherServiceImpl() {
  weather_data_map_["San Francisco"] = WeatherData::SUNNY;
  weather_data_map_["New York"] = WeatherData::CLOUDY;
  weather_data_map_["Seattle"] = WeatherData::RAINY;
  weather_data_map_["Denver"] = WeatherData::SNOWY;
}

Status FetcherServiceImpl::GetWeather(
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
