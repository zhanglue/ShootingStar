#include "src/weather_forecast/fetcher/fetcher_service.h"
#include "src/utilities/grpc_service_runner/grpc_service_runner.h"

namespace {

using Service = ::shooting_star::weather_flow::FetcherServiceImpl;
using ::shooting_star::utilities::RunGrpcService;

constexpr const char* kServiceName = "fetcher";

}  // namespace

int main(int argc, char** argv) {
  return RunGrpcService<Service>(kServiceName, argc, argv);
}
