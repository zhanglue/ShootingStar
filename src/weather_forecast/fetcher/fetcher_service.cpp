#include "src/weather_forecast/fetcher/fetcher_service.h"

#include <grpcpp/grpcpp.h>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace weather_flow {

using ::grpc::ServerContext;
using ::grpc::Status;
using ::grpc::StatusCode;
using ::std::cout;
using ::std::endl;
using ::std::invalid_argument;
using ::std::make_unique;
using ::std::string;
using ::std::unique_ptr;
using ::weather_flow::GetWeatherRequest;
using ::weather_flow::GetWeatherResponse;

namespace {

FetcherServiceImpl::WeatherDataMap CreateDefaultWeatherDataMap() {
  FetcherServiceImpl::WeatherDataMap weather_data_map;
  weather_data_map["San Francisco"] = WeatherData::SUNNY;
  weather_data_map["New York"] = WeatherData::CLOUDY;
  weather_data_map["Seattle"] = WeatherData::RAINY;
  weather_data_map["Denver"] = WeatherData::SNOWY;
  return weather_data_map;
}

}  // namespace

FetcherServiceImpl::FetcherServiceImpl(WeatherDataMap weather_data_map)
    : weather_data_map_(::std::move(weather_data_map)) {
  if (weather_data_map_.empty()) {
    throw invalid_argument("FetcherServiceImpl weather_data_map must not be empty.");
  }
}

unique_ptr<FetcherServiceImpl> FetcherServiceImpl::Create() {
  WeatherDataMap weather_data_map = CreateDefaultWeatherDataMap();
  unique_ptr<FetcherServiceImpl> server =
      make_unique<FetcherServiceImpl>(::std::move(weather_data_map));
  return server;
}

Status FetcherServiceImpl::GetWeather(ServerContext* context,
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
