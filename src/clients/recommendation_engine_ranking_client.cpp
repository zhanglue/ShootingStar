#include <getopt.h>
#include <grpcpp/grpcpp.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "protos/recommendation_engine/ranking.grpc.pb.h"
#include "src/clients/client_runtime.h"
#include "src/utilities/json_parser/json_parser.h"
#include "src/utilities/local_profile_loader/local_profile_loader.h"
#include "src/utilities/runtime_utilities/runtime_utilities.h"

using ::google::protobuf::Map;
using ::google::protobuf::Value;
using ::grpc::Channel;
using ::grpc::ClientContext;
using ::grpc::Status;
using ::recommendation_engine::CandidateItem;
using ::recommendation_engine::RankRequest;
using ::recommendation_engine::RankResponse;
using ::recommendation_engine::RankingOptions;
using ::recommendation_engine::ReasonType;
using ::recommendation_engine::RetrievalReason;
using ::recommendation_engine::TriggerEntity;
using ::shooting_star::utilities::GenerateGuid;
using ::shooting_star::utilities::JsonParser;
using ::shooting_star::utilities::LoadProfileFromLocalFile;
using ::shooting_star::utilities::ResolveWorkspaceRelativePath;
using ::std::chrono::duration_cast;
using ::std::chrono::steady_clock;
using ::std::cout;
using ::std::shared_ptr;
using ::std::string;
using ::std::string_view;
using ::std::unique_ptr;
using ::std::vector;

constexpr char kDefaultProfileDataPath[] =
    "tests/testdata/recommendation_engine/local_recommendation_fixture/profiles.jsonl";
constexpr char kDefaultRecallManifestPath[] =
    "tests/testdata/recommendation_engine/local_recommendation_fixture/manifest.json";

namespace recommendation_engine {
namespace {

struct RecallCandidates {
  vector<uint64_t> item_cf_items;
  vector<uint64_t> user_cf_items;
};

vector<uint64_t> ReadUint64ListField(const Map<string, Value>& fields,
                                     string_view field_name,
                                     string_view context) {
  const Value* value = JsonParser::FindField(fields, field_name);
  if (value == nullptr || JsonParser::IsNull(*value)) {
    return {};
  }
  if (value->kind_case() != Value::kListValue) {
    throw ::std::runtime_error(string(context) + "." + string(field_name) +
                               " must be an array");
  }

  vector<uint64_t> result;
  const auto& list = value->list_value();
  result.reserve(list.values_size());
  for (int i = 0; i < list.values_size(); ++i) {
    result.push_back(JsonParser::ReadUint64Value(
        list.values(i),
        string(context) + "." + string(field_name) + "[" +
            ::std::to_string(i) + "]"));
  }
  return result;
}

RecallCandidates LoadRecallCandidatesFromManifest(const string& manifest_path,
                                                  const string& executable_path,
                                                  int64_t user_id) {
  const string resolved_manifest_path =
      ResolveWorkspaceRelativePath(manifest_path, executable_path);

  ::std::ifstream manifest_file(resolved_manifest_path);
  if (!manifest_file.is_open()) {
    throw ::std::runtime_error("Failed to open manifest file: " +
                               resolved_manifest_path);
  }

  const string manifest_json(
      (::std::istreambuf_iterator<char>(manifest_file)),
      ::std::istreambuf_iterator<char>());
  const auto manifest =
      JsonParser::ParseObject(manifest_json, "ranking_recall_manifest");

  const auto& root_fields = manifest.fields();
  const Value& users_value = JsonParser::RequiredField(
      root_fields, "users", "ranking_recall_manifest");
  if (users_value.kind_case() != Value::kListValue) {
    throw ::std::runtime_error("ranking_recall_manifest.users must be an array");
  }

  for (int i = 0; i < users_value.list_value().values_size(); ++i) {
    const Value& user_value = users_value.list_value().values(i);
    if (user_value.kind_case() != Value::kStructValue) {
      throw ::std::runtime_error("ranking_recall_manifest.users[" +
                                 ::std::to_string(i) + "] must be an object");
    }

    const auto& user_fields = user_value.struct_value().fields();
    const int64_t manifest_user_id = JsonParser::ReadOptionalInt64(
        user_fields, "user_id",
        "ranking_recall_manifest.users[" + ::std::to_string(i) + "]");
    if (manifest_user_id != user_id) {
      continue;
    }

    const string user_context =
        "ranking_recall_manifest.users[" + ::std::to_string(i) + "]";
    return RecallCandidates{
        .item_cf_items =
            ReadUint64ListField(user_fields, "top_item_cf_items", user_context),
        .user_cf_items =
            ReadUint64ListField(user_fields, "top_user_cf_items", user_context),
    };
  }

  throw ::std::runtime_error(
      "User " + ::std::to_string(user_id) +
      " was not found in ranking recall manifest users list.");
}

double BuildRetrieverScore(int index, double base, double step) {
  const double score = base - static_cast<double>(index) * step;
  return ::std::max(0.01, score);
}

void AddCandidateWithReason(uint64_t item_id,
                            string_view retriever_name,
                            ReasonType reason_type,
                            int64_t user_id,
                            double retrieve_score,
                            RankRequest* request) {
  CandidateItem* candidate = request->add_candidates();
  candidate->set_item_id(item_id);
  candidate->set_item_type(ItemType::ITEM_TYPE_VIDEO);
  candidate->set_retriever(string(retriever_name));
  candidate->set_retrieve_score(retrieve_score);

  RetrievalReason* reason = candidate->add_reasons();
  reason->set_reason_type(reason_type);
  reason->set_reason_score(retrieve_score);
  reason->set_description("demo recall from fixture manifest");

  TriggerEntity* trigger = reason->mutable_trigger();
  trigger->set_entity_type(EntityType::ENTITY_TYPE_USER);
  trigger->set_entity_id(static_cast<uint64_t>(user_id));
}

class RankingClient {
 public:
  explicit RankingClient(shared_ptr<Channel> channel)
      : stub_(RankingService::NewStub(channel)) {}

  void Rank(int64_t user_id, int max_results, const string& profile_data_path,
            const string& recall_manifest_path,
            const string& requested_ranker,
            bool include_score_factors,
            bool include_item_features,
            const string& executable_path) {
    RankRequest request;
    request.set_trace_id(GenerateGuid());
    request.set_request_id(GenerateGuid());
    request.set_user_id(user_id);
    request.set_max_results(max_results);

    string error_msg;
    if (!LoadProfileFromLocalFile(
            ResolveWorkspaceRelativePath(profile_data_path, executable_path),
            user_id,
            request.mutable_profile(),
            &error_msg)) {
      ::std::cerr << "Failed to load profile: " << error_msg << ::std::endl;
      return;
    }

    const RecallCandidates recall_candidates = LoadRecallCandidatesFromManifest(
        recall_manifest_path, executable_path, user_id);

    for (int i = 0; i < static_cast<int>(recall_candidates.item_cf_items.size());
         ++i) {
      AddCandidateWithReason(
          recall_candidates.item_cf_items[i], "item_cf",
          ReasonType::REASON_TYPE_ITEM_CF, user_id,
          BuildRetrieverScore(i, 1.0, 0.03), &request);
    }
    for (int i = 0; i < static_cast<int>(recall_candidates.user_cf_items.size());
         ++i) {
      AddCandidateWithReason(
          recall_candidates.user_cf_items[i], "user_cf",
          ReasonType::REASON_TYPE_USER_CF, user_id,
          BuildRetrieverScore(i, 0.9, 0.03), &request);
    }

    RankingOptions* options = request.mutable_options();
    options->set_ranker(requested_ranker);
    options->set_include_score_factors(include_score_factors);
    options->set_include_item_features(include_item_features);

    cout << "Rank request:" << ::std::endl;
    cout << request.DebugString() << ::std::endl;

    RankResponse response;
    ClientContext context;

    const auto start = steady_clock::now();
    const Status status = stub_->Rank(&context, request, &response);
    const auto elapsed_ms =
        duration_cast<::std::chrono::milliseconds>(steady_clock::now() - start)
            .count();
    cout << "Rank RPC elapsed: " << elapsed_ms << " ms" << ::std::endl;

    if (status.ok()) {
      cout << ::std::endl;
      cout << "Rank result:" << ::std::endl;
      cout << response.DebugString() << ::std::endl;
      cout << ::std::endl;
    } else {
      ::std::cerr << "RPC failed: " << status.error_code() << ", "
                  << status.error_message() << ::std::endl;
    }
  }

 private:
  unique_ptr<RankingService::Stub> stub_;
};

void PrintUsage() {
  cout << "Usage: ranking_client [options]\n"
       << "Options:\n"
       << "  -h, --help                      Show this help message\n"
       << "  -i, --ip <IP>                   Set server IP (default: "
          "127.0.0.1)\n"
       << "  -p, --port <PORT>               Set server port (default: "
          "50300)\n"
       << "  -u, --user-id <USER_ID>         Add user ID to rank for "
          "(repeatable; default: {85566})\n"
       << "  -m, --max-results <COUNT>       Set max ranking results "
          "(default: 20)\n"
       << "  -f, --profile-data-path <PATH>  Set demo profile jsonl path\n"
       << "                                  (default: "
       << kDefaultProfileDataPath << ")\n"
       << "  -r, --recall-manifest-path <PATH> Set recall manifest json path\n"
       << "                                  (default: "
       << kDefaultRecallManifestPath << ")\n"
       << "  -k, --ranker <RANKER>           Set requested ranker "
          "(default: heuristic_v1)\n"
       << "      --include-score-factors     Include score factors "
          "(default: false)\n"
       << "      --include-item-features      Include item features "
          "(default: true)\n"
       << "      --exclude-item-features      Exclude item features\n";
}

}  // namespace
}  // namespace recommendation_engine

int main(int argc, char** argv) {
  string ip = "127.0.0.1";
  string port = "50300";
  vector<int64_t> user_ids = {};
  int64_t default_user_id = 85566;
  int max_results = 20;
  string profile_data_path = kDefaultProfileDataPath;
  string recall_manifest_path = kDefaultRecallManifestPath;
  string ranker = "heuristic_v1";
  bool include_score_factors = false;
  bool include_item_features = true;

  constexpr int kOptIncludeScoreFactors = 1001;
  constexpr int kOptIncludeItemFeatures = 1002;
  constexpr int kOptExcludeItemFeatures = 1003;

  struct option long_options[] = {
      {"help", no_argument, nullptr, 'h'},
      {"ip", required_argument, nullptr, 'i'},
      {"port", required_argument, nullptr, 'p'},
      {"user-id", required_argument, nullptr, 'u'},
      {"max-results", required_argument, nullptr, 'm'},
      {"profile-data-path", required_argument, nullptr, 'f'},
      {"recall-manifest-path", required_argument, nullptr, 'r'},
      {"ranker", required_argument, nullptr, 'k'},
      {"include-score-factors", no_argument, nullptr, kOptIncludeScoreFactors},
      {"include-item-features", no_argument, nullptr, kOptIncludeItemFeatures},
      {"exclude-item-features", no_argument, nullptr, kOptExcludeItemFeatures},
      {0, 0, 0, 0},
  };

  int opt;
  int option_index = 0;
  while ((opt = getopt_long(argc, argv, "hi:p:u:m:f:r:k:", long_options,
                            &option_index)) != -1) {
    switch (opt) {
      case 'h':
        recommendation_engine::PrintUsage();
        return 0;
      case 'i':
        ip = optarg;
        break;
      case 'p':
        port = optarg;
        break;
      case 'u':
        try {
          user_ids.push_back(::std::stoll(optarg));
        } catch (const ::std::invalid_argument&) {
          ::std::cerr << "Error: user_id is not a valid integer: " << optarg
                      << "\n";
          return 1;
        } catch (const ::std::out_of_range&) {
          ::std::cerr << "Error: user_id is out of range: " << optarg << "\n";
          return 1;
        }
        break;
      case 'm':
        try {
          max_results = ::std::stoi(optarg);
        } catch (const ::std::invalid_argument&) {
          ::std::cerr << "Error: max_results is not a valid integer: "
                      << optarg << "\n";
          return 1;
        } catch (const ::std::out_of_range&) {
          ::std::cerr << "Error: max_results is out of range: " << optarg
                      << "\n";
          return 1;
        }
        break;
      case 'f':
        profile_data_path = optarg;
        break;
      case 'r':
        recall_manifest_path = optarg;
        break;
      case 'k':
        ranker = optarg;
        break;
      case kOptIncludeScoreFactors:
        include_score_factors = true;
        break;
      case kOptIncludeItemFeatures:
        include_item_features = true;
        break;
      case kOptExcludeItemFeatures:
        include_item_features = false;
        break;
      default:
        recommendation_engine::PrintUsage();
        return 1;
    }
  }

  if (user_ids.empty()) {
    user_ids.push_back(default_user_id);
  }

  const string target_str = ip + ":" + port;
  ::shooting_star::clients::PrintRunStartedAtUtc();
  cout << "Connecting to gRPC server at: " << target_str << ::std::endl;
  cout << "Ranking for users:";
  for (int64_t user_id : user_ids) {
    cout << " " << user_id;
  }
  cout << ::std::endl;
  cout << "Using profile data from: " << profile_data_path << ::std::endl;
  cout << "Using recall manifest from: " << recall_manifest_path << ::std::endl;
  cout << "Requested max results: " << max_results << ::std::endl;
  cout << "Requested ranker: " << ranker << ::std::endl;
  cout << "Include score factors: "
       << (include_score_factors ? "true" : "false") << ::std::endl;
  cout << "Include item features: "
       << (include_item_features ? "true" : "false") << ::std::endl
       << ::std::endl;

  recommendation_engine::RankingClient client(
      grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));
  for (int64_t user_id : user_ids) {
    cout << "Ranking for user: " << user_id << ::std::endl;
    client.Rank(user_id, max_results, profile_data_path, recall_manifest_path,
                ranker, include_score_factors, include_item_features, argv[0]);
  }

  return 0;
}
