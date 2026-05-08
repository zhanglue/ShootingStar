#include "src/recommendation_engine/retrieval/retrievers/user_cf/retriever_user_cf.h"
#include "src/utilities/grpc_service_runner/grpc_service_runner.h"

namespace {

using Service = ::recommendation_engine::RetrieverUserCf;
using ::shooting_star::utilities::RunGrpcService;

constexpr const char* kServiceName = "retriever_user_cf";

}  // namespace

int main(int argc, char** argv) {
  return RunGrpcService<Service>(kServiceName, argc, argv);
}
