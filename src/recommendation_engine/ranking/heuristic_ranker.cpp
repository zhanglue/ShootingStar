#include "src/recommendation_engine/ranking/heuristic_ranker.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <grpcpp/grpcpp.h>

namespace shooting_star::recommendation_engine {

using ::grpc::Status;
using ::grpc::StatusCode;
using ::std::format;
using ::std::make_unique;
using ::std::optional;
using ::std::shared_ptr;
using ::std::string;
using ::std::string_view;
using ::std::unique_ptr;
using ::std::unordered_map;
using ::std::vector;

namespace {

constexpr double kRetrievalWeight = 1.0;
constexpr double kInterestWeight = 1.0;
constexpr double kNegativeFeedbackWeight = -1.0;
constexpr double kQualityWeight = 0.1;

void AddWeight(string_view name,
               double weight,
               unordered_map<string, double>* weights) {
  if (name.empty()) {
    return;
  }
  (*weights)[string(name)] += weight;
}

unordered_map<string, double> BuildGenreWeights(const Profile& profile) {
  unordered_map<string, double> weights;
  for (const GenreInterest& genre : profile.interests().genres()) {
    AddWeight(genre.genre(), genre.weight(), &weights);
  }
  return weights;
}

unordered_map<string, double> BuildTagWeights(const Profile& profile) {
  unordered_map<string, double> weights;
  for (const TagInterest& tag : profile.interests().tags()) {
    AddWeight(tag.tag(), tag.weight(), &weights);
  }
  return weights;
}

unordered_map<string, double> BuildNegativeGenreWeights(const Profile& profile) {
  unordered_map<string, double> weights;
  for (const GenreInterest& genre : profile.negative_feedbacks().genres()) {
    AddWeight(genre.genre(), genre.weight(), &weights);
  }
  return weights;
}

unordered_map<string, double> BuildNegativeTagWeights(const Profile& profile) {
  unordered_map<string, double> weights;
  for (const TagInterest& tag : profile.negative_feedbacks().tags()) {
    AddWeight(tag.tag(), tag.weight(), &weights);
  }
  return weights;
}

unordered_map<uint64_t, double> BuildNegativeItemWeights(
    const Profile& profile) {
  unordered_map<uint64_t, double> weights;
  for (const WeightedItem& item : profile.negative_feedbacks().items()) {
    if (item.item_id() <= 0) {
      continue;
    }
    weights[static_cast<uint64_t>(item.item_id())] += item.weight();
  }
  return weights;
}

double LookupWeight(const unordered_map<string, double>& weights,
                    string_view name) {
  const auto iter = weights.find(string(name));
  if (iter == weights.end()) {
    return 0.0;
  }
  return iter->second;
}

double LookupItemWeight(const unordered_map<uint64_t, double>& weights,
                        uint64_t item_id) {
  const auto iter = weights.find(item_id);
  if (iter == weights.end()) {
    return 0.0;
  }
  return iter->second;
}

RankingScoreFactor BuildScoreFactor(RankingScoreFactorType factor_type,
                                    string_view name,
                                    double raw_value,
                                    double weight,
                                    string_view description) {
  RankingScoreFactor factor;
  factor.set_factor_type(factor_type);
  factor.set_name(string(name));
  factor.set_raw_value(raw_value);
  factor.set_weight(weight);
  factor.set_contribution(raw_value * weight);
  factor.set_description(string(description));
  return factor;
}

void EnsureRetrievalSignals(CandidateItem* candidate) {
  if (candidate->retrieval_signals_size() > 0) {
    return;
  }

  RetrievalSignal* signal = candidate->add_retrieval_signals();
  signal->set_retriever(candidate->retriever());
  signal->set_retrieve_score(candidate->retrieve_score());
  for (const RetrievalReason& reason : candidate->reasons()) {
    signal->add_reasons()->CopyFrom(reason);
  }
}

string JoinRetrieverNames(const vector<string>& names) {
  string joined;
  for (const string& name : names) {
    if (name.empty()) {
      continue;
    }
    if (!joined.empty()) {
      joined += ",";
    }
    joined += name;
  }
  return joined;
}

void RebuildLegacyCandidateFields(CandidateItem* candidate) {
  double retrieve_score = 0.0;
  vector<string> retriever_names;
  unordered_map<string, bool> seen_retrievers;

  candidate->clear_reasons();
  for (const RetrievalSignal& signal : candidate->retrieval_signals()) {
    retrieve_score += signal.retrieve_score();
    if (!signal.retriever().empty() &&
        seen_retrievers.emplace(signal.retriever(), true).second) {
      retriever_names.push_back(signal.retriever());
    }
    for (const RetrievalReason& reason : signal.reasons()) {
      candidate->add_reasons()->CopyFrom(reason);
    }
  }

  candidate->set_retriever(JoinRetrieverNames(retriever_names));
  candidate->set_retrieve_score(retrieve_score);
}

void NormalizeCandidate(CandidateItem* candidate) {
  EnsureRetrievalSignals(candidate);
  RebuildLegacyCandidateFields(candidate);
}

void MergeRetrievalSignal(const RetrievalSignal& source_signal,
                          CandidateItem* target_candidate) {
  for (RetrievalSignal& target_signal :
       *target_candidate->mutable_retrieval_signals()) {
    if (target_signal.retriever() != source_signal.retriever()) {
      continue;
    }

    target_signal.set_retrieve_score(
        target_signal.retrieve_score() + source_signal.retrieve_score());
    for (const RetrievalReason& reason : source_signal.reasons()) {
      target_signal.add_reasons()->CopyFrom(reason);
    }
    return;
  }

  target_candidate->add_retrieval_signals()->CopyFrom(source_signal);
}

void MergeCandidate(CandidateItem source_candidate,
                    CandidateItem* target_candidate) {
  NormalizeCandidate(&source_candidate);

  if (target_candidate->item_type() == ItemType::ITEM_TYPE_UNSPECIFIED &&
      source_candidate.item_type() != ItemType::ITEM_TYPE_UNSPECIFIED) {
    target_candidate->set_item_type(source_candidate.item_type());
  }
  if (target_candidate->author_id() == 0 &&
      source_candidate.author_id() != 0) {
    target_candidate->set_author_id(source_candidate.author_id());
  }

  for (const RetrievalSignal& signal : source_candidate.retrieval_signals()) {
    MergeRetrievalSignal(signal, target_candidate);
  }
  RebuildLegacyCandidateFields(target_candidate);
}

vector<CandidateItem> NormalizeAndMergeCandidates(const RankRequest& request) {
  vector<CandidateItem> candidates;
  candidates.reserve(request.candidates_size());

  unordered_map<uint64_t, int> index_by_item_id;
  for (const CandidateItem& candidate : request.candidates()) {
    if (candidate.item_id() == 0) {
      continue;
    }

    const auto existing_iter = index_by_item_id.find(candidate.item_id());
    if (existing_iter != index_by_item_id.end()) {
      MergeCandidate(candidate, &candidates[existing_iter->second]);
      continue;
    }

    CandidateItem normalized_candidate;
    normalized_candidate.CopyFrom(candidate);
    NormalizeCandidate(&normalized_candidate);
    index_by_item_id.emplace(normalized_candidate.item_id(),
                             static_cast<int>(candidates.size()));
    candidates.push_back(::std::move(normalized_candidate));
  }

  return candidates;
}

double ComputeInterestScore(const ItemIndexEntry& item_index_entry,
                            const unordered_map<string, double>& genre_weights,
                            const unordered_map<string, double>& tag_weights) {
  double score = 0.0;
  for (const string& genre : item_index_entry.genres) {
    score += LookupWeight(genre_weights, genre);
  }
  for (const ItemIndexWeightedTag& tag : item_index_entry.top_tags) {
    if (tag.weight <= 0.0) {
      continue;
    }
    score += LookupWeight(tag_weights, tag.tag);
  }
  return score;
}

double ComputeNegativeFeedbackScore(
    const ItemIndexEntry& item_index_entry,
    const unordered_map<string, double>& negative_genre_weights,
    const unordered_map<string, double>& negative_tag_weights) {
  double score = 0.0;
  for (const string& genre : item_index_entry.genres) {
    score += LookupWeight(negative_genre_weights, genre);
  }
  for (const ItemIndexWeightedTag& tag : item_index_entry.top_tags) {
    if (tag.weight <= 0.0) {
      continue;
    }
    score += LookupWeight(negative_tag_weights, tag.tag);
  }
  return score;
}

double ComputeQualityScore(const ItemIndexEntry& item_index_entry) {
  if (item_index_entry.rating.avg <= 0.0 ||
      item_index_entry.rating.count <= 0) {
    return 0.0;
  }
  return (item_index_entry.rating.avg / 5.0) *
         ::std::log1p(static_cast<double>(item_index_entry.rating.count));
}

void FillItemFeatures(const ItemIndexEntry& item_index_entry,
                      RankingItemFeatures* item_features) {
  item_features->set_item_id(item_index_entry.item_id);
  item_features->set_title(item_index_entry.title);
  item_features->set_year(item_index_entry.year);
  for (const string& genre : item_index_entry.genres) {
    item_features->add_genres(genre);
  }
  for (const ItemIndexWeightedTag& tag : item_index_entry.top_tags) {
    RankingWeightedTag* top_tag = item_features->add_top_tags();
    top_tag->set_tag(tag.tag);
    top_tag->set_weight(tag.weight);
  }
  item_features->mutable_rating()->set_avg(item_index_entry.rating.avg);
  item_features->mutable_rating()->set_count(item_index_entry.rating.count);
}

}  // namespace

HeuristicRankTask::HeuristicRankTask(
    const RankRequest& request,
    RankResponse* response,
    shared_ptr<const ItemIndexStore> item_index_store)
    : request_(request),
      response_(response),
      item_index_store_(::std::move(item_index_store)),
      genre_weights_(BuildGenreWeights(request.profile())),
      tag_weights_(BuildTagWeights(request.profile())),
      negative_genre_weights_(BuildNegativeGenreWeights(request.profile())),
      negative_tag_weights_(BuildNegativeTagWeights(request.profile())),
      negative_item_weights_(BuildNegativeItemWeights(request.profile())) {}

Status HeuristicRankTask::Run() {
  if (response_ == nullptr) {
    return Status(StatusCode::INTERNAL, "Rank response must not be null.");
  }
  response_->set_msg("");
  if (item_index_store_ == nullptr) {
    response_->set_status(RankingServiceStatus::RANKING_SYSTEM_ERROR);
    response_->set_msg("Item index store is not initialized.");
    return Status(StatusCode::INTERNAL,
                  "Item index store is not initialized.");
  }

  vector<CandidateItem> candidates = NormalizeAndMergeCandidates(request_);
  if (candidates.empty()) {
    response_->set_status(RankingServiceStatus::RANKING_EMPTY_CANDIDATES);
    response_->set_msg("No candidates available for ranking.");
    response_->set_ranked_candidate_count(0);
    return Status::OK;
  }

  vector<uint64_t> item_ids;
  item_ids.reserve(candidates.size());
  for (const CandidateItem& candidate : candidates) {
    item_ids.push_back(candidate.item_id());
  }

  const vector<optional<ItemIndexEntry>> item_index_entries =
      item_index_store_->FindByItemIds(item_ids);
  if (item_index_entries.size() != candidates.size()) {
    response_->set_status(RankingServiceStatus::RANKING_SYSTEM_ERROR);
    response_->set_msg(
        format("Item index store returned {} entries for {} candidates.",
               item_index_entries.size(), candidates.size()));
    return Status(
        StatusCode::INTERNAL,
        format("Item index store returned {} entries for {} candidates.",
               item_index_entries.size(), candidates.size()));
  }

  vector<ScoredCandidate> scored_candidates =
      ScoreCandidates(candidates, item_index_entries);
  ::std::stable_sort(
      scored_candidates.begin(),
      scored_candidates.end(),
      [](const ScoredCandidate& lhs, const ScoredCandidate& rhs) {
        if (lhs.rank_score != rhs.rank_score) {
          return lhs.rank_score > rhs.rank_score;
        }
        return lhs.candidate.item_id() < rhs.candidate.item_id();
      });

  const int max_results = ::std::min(
      request_.max_results(), static_cast<int>(scored_candidates.size()));
  for (int index = 0; index < max_results; ++index) {
    FillRankedCandidate(scored_candidates[index],
                        index + 1,
                        response_->add_ranked_candidates());
  }

  response_->set_status(RankingServiceStatus::RANKING_SUCCESS);
  response_->set_msg("");
  response_->set_ranked_candidate_count(response_->ranked_candidates_size());
  return Status::OK;
}

vector<HeuristicRankTask::ScoredCandidate>
HeuristicRankTask::ScoreCandidates(
    const vector<CandidateItem>& candidates,
    const vector<optional<ItemIndexEntry>>& item_index_entries) const {
  vector<ScoredCandidate> scored_candidates;
  scored_candidates.reserve(candidates.size());

  for (int index = 0; index < static_cast<int>(candidates.size()); ++index) {
    ScoredCandidate scored_candidate;
    scored_candidate.candidate.CopyFrom(candidates[index]);
    scored_candidate.item_index_entry = item_index_entries[index];

    AddFactor(candidates[index].retrieve_score(),
              kRetrievalWeight,
              RankingScoreFactorType::RANKING_SCORE_FACTOR_TYPE_RETRIEVAL,
              "retrieval",
              "Aggregated retrieval score.",
              &scored_candidate);

    double negative_feedback_score =
        LookupItemWeight(negative_item_weights_, candidates[index].item_id());

    if (scored_candidate.item_index_entry.has_value()) {
      AddItemFactors(*scored_candidate.item_index_entry,
                     &negative_feedback_score,
                     &scored_candidate);
    }

    AddFactor(negative_feedback_score,
              kNegativeFeedbackWeight,
              RankingScoreFactorType::
                  RANKING_SCORE_FACTOR_TYPE_NEGATIVE_FEEDBACK,
              "negative_feedback",
              "Item, genre, and tag overlap with negative feedback.",
              &scored_candidate);

    scored_candidates.push_back(::std::move(scored_candidate));
  }

  return scored_candidates;
}

void HeuristicRankTask::AddItemFactors(
    const ItemIndexEntry& item_index_entry,
    double* negative_feedback_score,
    ScoredCandidate* scored_candidate) const {
  AddFactor(ComputeInterestScore(item_index_entry, genre_weights_,
                                 tag_weights_),
            kInterestWeight,
            RankingScoreFactorType::RANKING_SCORE_FACTOR_TYPE_INTEREST,
            "interest_match",
            "Genre and tag overlap with positive interests.",
            scored_candidate);
  *negative_feedback_score += ComputeNegativeFeedbackScore(
      item_index_entry, negative_genre_weights_, negative_tag_weights_);
  AddFactor(ComputeQualityScore(item_index_entry),
            kQualityWeight,
            RankingScoreFactorType::RANKING_SCORE_FACTOR_TYPE_QUALITY,
            "quality",
            "Rating average with log-scaled rating count.",
            scored_candidate);
}

void HeuristicRankTask::AddFactor(double raw_value,
                                  double weight,
                                  RankingScoreFactorType factor_type,
                                  string_view name,
                                  string_view description,
                                  ScoredCandidate* scored_candidate) const {
  const RankingScoreFactor factor = BuildScoreFactor(
      factor_type, name, raw_value, weight, description);
  scored_candidate->rank_score += factor.contribution();
  scored_candidate->score_factors.push_back(factor);
}

void HeuristicRankTask::FillRankedCandidate(
    const ScoredCandidate& scored_candidate,
    int rank_position,
    RankedCandidateItem* ranked_candidate) const {
  ranked_candidate->mutable_candidate()->CopyFrom(scored_candidate.candidate);
  ranked_candidate->set_rank_score(scored_candidate.rank_score);
  ranked_candidate->set_rank_position(rank_position);
  ranked_candidate->set_ranker(string(HeuristicRanker::kName));
  ranked_candidate->mutable_retrieval_signals()->CopyFrom(
      scored_candidate.candidate.retrieval_signals());

  if (request_.options().include_score_factors()) {
    for (const RankingScoreFactor& factor : scored_candidate.score_factors) {
      ranked_candidate->add_score_factors()->CopyFrom(factor);
    }
  }

  if (request_.options().include_item_features() &&
      scored_candidate.item_index_entry.has_value()) {
    FillItemFeatures(*scored_candidate.item_index_entry,
                     ranked_candidate->mutable_item_features());
  }
}

HeuristicRanker::HeuristicRanker(shared_ptr<const ItemIndexStore> item_index_store)
    : item_index_store_(::std::move(item_index_store)) {
  if (item_index_store_ == nullptr) {
    throw ::std::invalid_argument("HeuristicRanker item_index_store is null.");
  }
}

string_view HeuristicRanker::Name() const {
  return kName;
}

unique_ptr<RankTask> HeuristicRanker::CreateTask(
    const RankRequest& request,
    RankResponse* response) const {
  return make_unique<HeuristicRankTask>(request, response, item_index_store_);
}

}  // namespace shooting_star::recommendation_engine
