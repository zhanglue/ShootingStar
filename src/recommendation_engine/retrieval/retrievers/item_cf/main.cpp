#include "src/recommendation_engine/retrieval/retrievers/item_cf/retriever_item_cf.h"
#include "src/utilities/grpc_service_runner/grpc_service_runner.h"

namespace {

constexpr const char* kServiceName = "retriever_item_cf";

}  // namespace

int main(int argc, char** argv) {
  return ::shooting_star::utilities::RunGrpcService<
      ::recommendation_engine::RetrieverItemCf>(kServiceName, argc, argv);
}
