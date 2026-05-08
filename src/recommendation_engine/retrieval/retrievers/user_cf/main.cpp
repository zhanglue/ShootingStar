#include "src/recommendation_engine/retrieval/retrievers/user_cf/retriever_user_cf.h"
#include "src/utilities/grpc_service_runner/grpc_service_runner.h"

namespace {

constexpr const char* kServiceName = "retriever_user_cf";

}  // namespace

int main(int argc, char** argv) {
  return ::shooting_star::utilities::RunGrpcService<
      ::recommendation_engine::RetrieverUserCf>(kServiceName, argc, argv);
}
