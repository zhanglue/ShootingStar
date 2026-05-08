#include "src/recommendation_engine/retrieval/orchestrator/retrieval_orchestrator.h"
#include "src/utilities/grpc_service_runner/grpc_service_runner.h"

namespace {

constexpr const char* kServiceName = "retrieval_orchestrator";

}  // namespace

int main(int argc, char** argv) {
  return ::shooting_star::utilities::RunGrpcService<
      ::recommendation_engine::RetrievalOrchestrator>(kServiceName, argc, argv);
}
