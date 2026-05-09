#include "src/recommendation_engine/ranking/ranking_service.h"

#include <chrono>
#include <cstddef>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "src/recommendation_engine/ranking/caching_item_index_store.h"
#include "src/recommendation_engine/ranking/elasticsearch_item_index_store.h"
#include "src/recommendation_engine/ranking/heuristic_ranker.h"
#include "src/recommendation_engine/ranking/local_file_item_index_store.h"
#include "src/utilities/elasticsearch_client/elasticsearch_client.h"
#include "src/utilities/logger/logger.h"
#include "src/utilities/logger/logger_registry.h"
#include "src/utilities/runtime_utilities/runtime_utilities.h"

namespace shooting_star::recommendation_engine {

using ::grpc::ServerContext;
using ::grpc::Status;
using ::grpc::StatusCode;
using ::shooting_star::utilities::ElasticsearchClient;
using ::shooting_star::utilities::GlobalConfig;
using ::shooting_star::utilities::Logger;
using ::shooting_star::utilities::LoggerRegistry;
using ::shooting_star::utilities::ResolveWorkspaceRelativePath;
using ::std::chrono::milliseconds;
using ::std::format;
using ::std::make_unique;
using ::std::shared_ptr;
using ::std::size_t;
using ::std::string;
using ::std::string_view;
using ::std::unique_ptr;
using ::std::vector;

namespace {

Status InvalidRequest(RankResponse* response, string_view message) {
  if (response != nullptr) {
    response->set_status(RankingServiceStatus::RANKING_INVALID_REQUEST);
    response->set_msg(string(message));
  }
  return Status(StatusCode::INVALID_ARGUMENT, string(message));
}

Status ValidateRankRequest(const RankRequest& request, RankResponse* response) {
  if (request.user_id() <= 0) {
    return InvalidRequest(
        response, format("Invalid user_id {}.", request.user_id()));
  }
  if (!request.has_profile()) {
    return InvalidRequest(
        response, format("Profile is required for user {}.",
                         request.user_id()));
  }
  if (request.profile().user_id() != 0 &&
      request.profile().user_id() != request.user_id()) {
    return InvalidRequest(
        response,
        format("Profile user_id {} does not match request user_id {}.",
               request.profile().user_id(), request.user_id()));
  }
  if (request.max_results() <= 0) {
    return InvalidRequest(
        response, format("Invalid max_results {}.", request.max_results()));
  }
  return Status::OK;
}

unique_ptr<ItemIndexStore> WrapWithLocalCacheIfConfigured(
    const GlobalConfig& config,
    unique_ptr<ItemIndexStore> item_index_store) {
  const Logger& logger = LoggerRegistry::Get();
  const int capacity = config.GetLocalCacheCapacity();
  const int ttl_seconds = config.GetLocalCacheTtlSeconds();
  if (capacity <= 0) {
    logger.Info(
        "item_index_local_cache_disabled",
        {
            {"reason", string(config.GetLocalCacheCapacityKey()) +
                           " must be greater than 0"},
            {"item_index_local_cache_capacity", ::std::to_string(capacity)},
            {"item_index_local_cache_ttl_seconds",
             ::std::to_string(ttl_seconds)},
        });
    return item_index_store;
  }
  if (ttl_seconds <= 0) {
    logger.Info(
        "item_index_local_cache_disabled",
        {
            {"reason", string(config.GetLocalCacheTtlSecondsKey()) +
                           " must be greater than 0"},
            {"item_index_local_cache_capacity", ::std::to_string(capacity)},
            {"item_index_local_cache_ttl_seconds",
             ::std::to_string(ttl_seconds)},
        });
    return item_index_store;
  }

  logger.Info(
      "item_index_local_cache_initialized",
      {
          {"item_index_local_cache_capacity", ::std::to_string(capacity)},
          {"item_index_local_cache_ttl_seconds", ::std::to_string(ttl_seconds)},
      });
  return make_unique<CachingItemIndexStore>(
      ::std::move(item_index_store), static_cast<size_t>(capacity),
      ::std::chrono::duration_cast<milliseconds>(
          ::std::chrono::seconds(ttl_seconds)));
}

unique_ptr<ItemIndexStore> CreateUncachedItemIndexStore(
    const GlobalConfig& config) {
  const Logger& logger = LoggerRegistry::Get();
  const string store_type = config.GetItemIndexStoreType();

  if (store_type == ItemIndexStore::kLocalStoreType) {
    const string item_index_data_path =
        ResolveWorkspaceRelativePath(config.GetItemIndexStoreDataPath());
    logger.Info(
        "item_index_store_initialized",
        {
            {"item_index_store_type", store_type},
            {"item_index_data_path", item_index_data_path},
        });
    return make_unique<LocalFileItemIndexStore>(item_index_data_path);
  }

  if (store_type == ItemIndexStore::kElasticsearchStoreType) {
    logger.Info(
        "item_index_store_initialized",
        {
            {"item_index_store_type", store_type},
            {"item_index_es_base_url", config.GetElasticsearchBaseUrl()},
            {"item_index_es_index", config.GetElasticsearchIndex()},
            {"item_index_es_request_timeout_ms",
             ::std::to_string(config.GetElasticsearchRequestTimeoutMs())},
        });
    return make_unique<ElasticsearchItemIndexStore>(
        ElasticsearchClient::Create(), config.GetElasticsearchIndex());
  }

  throw ::std::invalid_argument(
      format("Unsupported item index store type: {}.", store_type));
}

}  // namespace

RankingServiceImpl::RankingServiceImpl(
    shared_ptr<const ItemIndexStore> item_index_store,
    vector<unique_ptr<Ranker>> rankers,
    string default_ranker_name)
    : item_index_store_(::std::move(item_index_store)) {
  if (item_index_store_ == nullptr) {
    throw ::std::invalid_argument(
        "RankingServiceImpl item_index_store must not be null.");
  }
  RegisterRankers(::std::move(rankers));
  RegisterDefaultRanker(::std::move(default_ranker_name));
}

unique_ptr<RankingServiceImpl> RankingServiceImpl::Create(
    const GlobalConfig& config) {
  shared_ptr<const ItemIndexStore> item_index_store =
      CreateItemIndexStore(config);
  vector<unique_ptr<Ranker>> rankers = CreateRankers(config, item_index_store);
  const string default_ranker_name = config.GetRankingDefaultRanker();

  unique_ptr<RankingServiceImpl> server = make_unique<RankingServiceImpl>(
      ::std::move(item_index_store), ::std::move(rankers),
      default_ranker_name);
  return server;
}

shared_ptr<const ItemIndexStore> RankingServiceImpl::CreateItemIndexStore(
    const GlobalConfig& config) {
  unique_ptr<ItemIndexStore> item_index_store =
      WrapWithLocalCacheIfConfigured(config,
                                     CreateUncachedItemIndexStore(config));
  return shared_ptr<const ItemIndexStore>(::std::move(item_index_store));
}

vector<unique_ptr<Ranker>> RankingServiceImpl::CreateRankers(
    const GlobalConfig& config,
    shared_ptr<const ItemIndexStore> item_index_store) {
  if (item_index_store == nullptr) {
    throw ::std::invalid_argument(
        "RankingServiceImpl item_index_store is null.");
  }

  const vector<string> ranker_names = config.GetRankingRankers();
  if (ranker_names.empty()) {
    throw ::std::invalid_argument("ranking.rankers must not be empty.");
  }

  vector<unique_ptr<Ranker>> rankers;
  rankers.reserve(ranker_names.size());
  for (const string& ranker_name : ranker_names) {
    rankers.push_back(CreateRanker(ranker_name, item_index_store));
  }
  return rankers;
}

unique_ptr<Ranker> RankingServiceImpl::CreateRanker(
    string_view ranker_name,
    shared_ptr<const ItemIndexStore> item_index_store) {
  if (ranker_name == HeuristicRanker::kName) {
    return make_unique<HeuristicRanker>(::std::move(item_index_store));
  }

  throw ::std::invalid_argument(
      format("Unsupported configured ranker: {}.", ranker_name));
}

void RankingServiceImpl::RegisterRankers(vector<unique_ptr<Ranker>> rankers) {
  if (rankers.empty()) {
    throw ::std::invalid_argument("RankingServiceImpl rankers is empty.");
  }

  for (unique_ptr<Ranker>& ranker : rankers) {
    RegisterRanker(::std::move(ranker));
  }
}

void RankingServiceImpl::RegisterRanker(unique_ptr<Ranker> ranker) {
  if (ranker == nullptr) {
    throw ::std::invalid_argument("RankingServiceImpl ranker is null.");
  }
  const string ranker_name(ranker->Name());
  if (ranker_name.empty()) {
    throw ::std::invalid_argument("RankingServiceImpl ranker name is empty.");
  }
  if (!rankers_.emplace(ranker_name, ::std::move(ranker)).second) {
    throw ::std::invalid_argument(
        format("Duplicated ranker name {}.", ranker_name));
  }
}

void RankingServiceImpl::RegisterDefaultRanker(string default_ranker_name) {
  if (default_ranker_name.empty()) {
    throw ::std::invalid_argument("ranking.default_ranker must not be empty.");
  }
  if (FindRanker(default_ranker_name) == nullptr) {
    throw ::std::invalid_argument(
        format("Default ranker {} is not registered.", default_ranker_name));
  }

  default_ranker_name_ = ::std::move(default_ranker_name);
}

Status RankingServiceImpl::Rank(ServerContext* context,
                                const RankRequest* request,
                                RankResponse* response) {
  (void)context;
  const Logger& logger = LoggerRegistry::Get();

  // Validate the raw RPC pointers before reading the protobuf payload.
  if (request == nullptr || response == nullptr) {
    if (response != nullptr) {
      response->set_status(RankingServiceStatus::RANKING_SYSTEM_ERROR);
      response->set_msg("Rank request/response is null.");
    }
    logger.Error(
        "ranking_request_pointer_invalid",
        {
            {"request_is_null", request == nullptr ? "true" : "false"},
            {"response_is_null", response == nullptr ? "true" : "false"},
        });
    return Status(StatusCode::INTERNAL, "Rank request/response is null.");
  }

  // Log the incoming request at the RPC boundary.
  logger.Info(
      "ranking_request_received",
      {
          {"trace_id", request->trace_id()},
          {"request_id", request->request_id()},
          {"user_id", ::std::to_string(request->user_id())},
          {"candidate_count", ::std::to_string(request->candidates_size())},
          {"max_results", ::std::to_string(request->max_results())},
          {"requested_ranker", request->options().ranker()},
      });

  // Initialize response metadata before the ranking pipeline starts.
  response->set_msg("");
  response->set_input_candidate_count(request->candidates_size());
  response->set_ranked_candidate_count(0);

  // Validate business-level request fields.
  const Status validation_status = ValidateRankRequest(*request, response);
  if (!validation_status.ok()) {
    logger.Warning(
        "ranking_request_validation_failed",
        {
            {"trace_id", request->trace_id()},
            {"request_id", request->request_id()},
            {"user_id", ::std::to_string(request->user_id())},
            {"grpc_status_code",
             ::std::to_string(static_cast<int>(
                 validation_status.error_code()))},
            {"error_message", validation_status.error_message()},
        });
    return validation_status;
  }

  // Resolve the configured ranker for this request.
  const string ranker_name = ResolveRankerName(*request);
  const Ranker* ranker = FindRanker(ranker_name);
  if (ranker == nullptr) {
    response->set_status(RankingServiceStatus::RANKING_INVALID_REQUEST);
    response->set_msg(format("Unknown ranker {}.", ranker_name));
    logger.Warning(
        "ranking_ranker_not_found",
        {
            {"trace_id", request->trace_id()},
            {"request_id", request->request_id()},
            {"user_id", ::std::to_string(request->user_id())},
            {"ranker", ranker_name},
        });
    return Status(StatusCode::INVALID_ARGUMENT,
                  format("Unknown ranker {}.", ranker_name));
  }

  logger.Info(
      "ranking_ranker_selected",
      {
          {"trace_id", request->trace_id()},
          {"request_id", request->request_id()},
          {"user_id", ::std::to_string(request->user_id())},
          {"ranker", ranker_name},
      });

  try {
    // Create the request-scoped task from the selected ranker.
    unique_ptr<RankTask> task = ranker->CreateTask(*request, response);
    if (task == nullptr) {
      response->set_status(RankingServiceStatus::RANKING_SYSTEM_ERROR);
      response->set_msg(format("Ranker {} created a null task.", ranker_name));
      logger.Error(
          "ranking_task_create_failed",
          {
              {"trace_id", request->trace_id()},
              {"request_id", request->request_id()},
              {"user_id", ::std::to_string(request->user_id())},
              {"ranker", ranker_name},
              {"reason", "ranker returned null task"},
          });
      return Status(StatusCode::INTERNAL,
                    format("Ranker {} created a null task.", ranker_name));
    }

    logger.Debug(
        "ranking_task_created",
        {
            {"trace_id", request->trace_id()},
            {"request_id", request->request_id()},
            {"user_id", ::std::to_string(request->user_id())},
            {"ranker", ranker_name},
        });

    // Run the request-scoped ranking task and log the final outcome.
    const Status task_status = task->Run();
    if (!task_status.ok()) {
      if (response->msg().empty()) {
        response->set_msg(task_status.error_message());
      }
      logger.Error(
          "ranking_task_failed",
          {
              {"trace_id", request->trace_id()},
              {"request_id", request->request_id()},
              {"user_id", ::std::to_string(request->user_id())},
              {"ranker", ranker_name},
              {"grpc_status_code",
               ::std::to_string(static_cast<int>(task_status.error_code()))},
              {"error_message", task_status.error_message()},
              {"ranking_status",
               ::std::to_string(static_cast<int>(response->status()))},
          });
      return task_status;
    }
    if (response->status() == RankingServiceStatus::RANKING_SUCCESS) {
      response->set_msg("");
    } else if (response->msg().empty()) {
      response->set_msg(
          format("Ranking finished with status {}.",
                 static_cast<int>(response->status())));
    }

    logger.Info(
        "ranking_request_succeeded",
        {
            {"trace_id", request->trace_id()},
            {"request_id", request->request_id()},
            {"user_id", ::std::to_string(request->user_id())},
            {"ranker", ranker_name},
            {"input_candidate_count",
             ::std::to_string(response->input_candidate_count())},
            {"ranked_candidate_count",
             ::std::to_string(response->ranked_candidate_count())},
            {"ranking_status",
             ::std::to_string(static_cast<int>(response->status()))},
        });
    return task_status;
  } catch (const ::std::exception& ex) {
    response->set_status(RankingServiceStatus::RANKING_SYSTEM_ERROR);
    response->set_msg(ex.what());
    logger.Error(
        "ranking_request_failed",
        {
            {"trace_id", request->trace_id()},
            {"request_id", request->request_id()},
            {"user_id", ::std::to_string(request->user_id())},
            {"ranker", ranker_name},
            {"error_message", ex.what()},
        });
    return Status(StatusCode::INTERNAL, ex.what());
  }
}

const Ranker* RankingServiceImpl::FindRanker(string_view ranker_name) const {
  const auto iter = rankers_.find(string(ranker_name));
  if (iter == rankers_.end()) {
    return nullptr;
  }
  return iter->second.get();
}

string RankingServiceImpl::ResolveRankerName(const RankRequest& request) const {
  if (!request.options().ranker().empty()) {
    return request.options().ranker();
  }
  return default_ranker_name_;
}

}  // namespace shooting_star::recommendation_engine
