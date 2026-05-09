#include "src/recommendation_engine/profile/profile_service.h"
#include "src/utilities/grpc_service_runner/grpc_service_runner.h"

namespace {

using Service = ::shooting_star::recommendation_engine::ProfileServiceImpl;
using ::shooting_star::utilities::RunGrpcService;

constexpr const char* kServiceName = "profile";

}  // namespace

int main(int argc, char** argv) {
  return RunGrpcService<Service>(kServiceName, argc, argv);
}
