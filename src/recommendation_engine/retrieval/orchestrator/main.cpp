#include "src/recommendation_engine/retrieval/orchestrator/retrieval_orchestrator.h"
#include "src/utilities/grpc_service_runner/grpc_service_runner.h"

namespace {

using Service = ::shooting_star::recommendation_engine::RetrievalOrchestrator;
using ::shooting_star::utilities::RunGrpcService;

constexpr const char* kServiceName = "retrieval_orchestrator";

}  // namespace

int main(int argc, char** argv) {
  return RunGrpcService<Service>(kServiceName, argc, argv);
}
