#include "src/recommendation_engine/ranking/ranking_service.h"
#include "src/utilities/grpc_service_runner/grpc_service_runner.h"

namespace {

constexpr const char* kServiceName = "ranking";

}  // namespace

int main(int argc, char** argv) {
  return ::shooting_star::utilities::RunGrpcService<
      ::recommendation_engine::RankingServiceImpl>(kServiceName, argc, argv);
}
