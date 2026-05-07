#pragma once

#include <memory>
#include <string_view>

#include <grpcpp/grpcpp.h>

#include "protos/recommendation_engine/ranking.pb.h"

namespace recommendation_engine {

class RankTask {
 public:
  virtual ~RankTask() = default;

  virtual ::grpc::Status Run() = 0;
};

class Ranker {
 public:
  virtual ~Ranker() = default;

  virtual ::std::string_view Name() const = 0;
  virtual ::std::unique_ptr<RankTask> CreateTask(
      const RankRequest& request,
      RankResponse* response) const = 0;
};

}  // namespace recommendation_engine
