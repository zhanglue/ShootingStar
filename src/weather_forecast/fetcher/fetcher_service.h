#ifndef FETCHER_SERVICE_H
#define FETCHER_SERVICE_H

#include <memory>
#include <string>
#include <unordered_map>

#include <grpcpp/grpcpp.h>

#include "protos/weather_forecast/fetcher.grpc.pb.h"

namespace weather_flow {

class FetcherServiceImpl final : public Fetcher::Service {
 public:
  using WeatherDataMap =
      std::unordered_map<std::string, WeatherData::WeatherCondition>;

  explicit FetcherServiceImpl(WeatherDataMap weather_data_map);

  static std::unique_ptr<FetcherServiceImpl> Create();

  grpc::Status GetWeather(grpc::ServerContext* context,
                          const GetWeatherRequest* request,
                          GetWeatherResponse* response) override;

 private:
  WeatherDataMap weather_data_map_;
};

}  // namespace weather_flow

#endif  // FETCHER_SERVICE_H
