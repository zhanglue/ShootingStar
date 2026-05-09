#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace shooting_star::recommendation_engine {

struct LocalFileSimilarityNeighbor {
  uint64_t id = 0;
  double score = 0.0;
};

struct LocalFileSimilarityRow {
  uint64_t entity_id = 0;
  ::std::vector<LocalFileSimilarityNeighbor> neighbors;
};

LocalFileSimilarityRow ParseLocalFileSimilarityJsonlLine(
    ::std::string_view line,
    ::std::string_view entity_id_field,
    ::std::string_view neighbor_id_field,
    ::std::string_view context);

}  // namespace shooting_star::recommendation_engine
