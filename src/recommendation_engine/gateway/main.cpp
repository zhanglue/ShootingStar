#include "src/recommendation_engine/gateway/gateway_service.h"
#include "src/utilities/grpc_service_runner/grpc_service_runner.h"

namespace {

using Service = ::recommendation_engine::GatewayServiceImpl;
using ::shooting_star::utilities::RunGrpcService;

constexpr const char* kServiceName = "gateway";

}  // namespace

int main(int argc, char** argv) {
  return RunGrpcService<Service>(kServiceName, argc, argv);
}
