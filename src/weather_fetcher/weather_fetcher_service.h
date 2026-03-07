#ifndef WEATHER_FETCHER_SERVICE_H
#define WEATHER_FETCHER_SERVICE_H

#include <unordered_map>

#include <grpcpp/grpcpp.h>

#include "protos/weather_fetcher.grpc.pb.h"

namespace weather_flow {

class WeatherFetcherServiceImpl final : public WeatherFetcher::Service {
 public:
  WeatherFetcherServiceImpl();
  grpc::Status GetWeather(grpc::ServerContext* context,
                          const GetWeatherRequest* request,
                          GetWeatherResponse* response) override;

 private:
  std::unordered_map<std::string, WeatherData::WeatherCondition> weather_data_map_;
};

}  // namespace weather_flow

#endif  // WEATHER_FETCHER_SERVICE_H
