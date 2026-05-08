#include "src/recommendation_engine/gateway/gateway_service.h"
#include "src/utilities/grpc_service_runner/grpc_service_runner.h"

namespace {

constexpr const char* kServiceName = "gateway";

}  // namespace

int main(int argc, char** argv) {
  return ::shooting_star::utilities::RunGrpcService<
      ::recommendation_engine::GatewayServiceImpl>(kServiceName, argc, argv);
}
