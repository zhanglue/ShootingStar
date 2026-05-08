#include "src/recommendation_engine/profile/profile_service.h"
#include "src/utilities/grpc_service_runner/grpc_service_runner.h"

namespace {

constexpr const char* kServiceName = "profile";

}  // namespace

int main(int argc, char** argv) {
  return ::shooting_star::utilities::RunGrpcService<
      ::recommendation_engine::ProfileServiceImpl>(kServiceName, argc, argv);
}
