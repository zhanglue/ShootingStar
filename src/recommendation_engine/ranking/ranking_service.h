#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "protos/recommendation_engine/ranking.grpc.pb.h"
#include "src/recommendation_engine/ranking/item_index_store.h"
#include "src/recommendation_engine/ranking/ranker.h"
#include "src/utilities/global_config/global_config.h"

namespace recommendation_engine {

class RankingServiceImpl final : public RankingService::Service {
 public:
  RankingServiceImpl(
      ::std::shared_ptr<const ItemIndexStore> item_index_store,
      ::std::vector<::std::unique_ptr<Ranker>> rankers,
      ::std::string default_ranker_name);

  static ::std::unique_ptr<RankingServiceImpl> Create(
      const ::shooting_star::utilities::GlobalConfig& config);

  ::grpc::Status Rank(::grpc::ServerContext* context,
                      const RankRequest* request,
                      RankResponse* response) override;

 private:
  static ::std::shared_ptr<const ItemIndexStore> CreateItemIndexStore(
      const ::shooting_star::utilities::GlobalConfig& config);
  static ::std::vector<::std::unique_ptr<Ranker>> CreateRankers(
      const ::shooting_star::utilities::GlobalConfig& config,
      ::std::shared_ptr<const ItemIndexStore> item_index_store);
  static ::std::unique_ptr<Ranker> CreateRanker(
      ::std::string_view ranker_name,
      ::std::shared_ptr<const ItemIndexStore> item_index_store);
  void RegisterRankers(::std::vector<::std::unique_ptr<Ranker>> rankers);
  void RegisterRanker(::std::unique_ptr<Ranker> ranker);
  void RegisterDefaultRanker(::std::string default_ranker_name);
  const Ranker* FindRanker(::std::string_view ranker_name) const;
  ::std::string ResolveRankerName(const RankRequest& request) const;

  ::std::shared_ptr<const ItemIndexStore> item_index_store_;
  ::std::unordered_map<::std::string, ::std::unique_ptr<Ranker>> rankers_;
  ::std::string default_ranker_name_;
};

}  // namespace recommendation_engine
