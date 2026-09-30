// GPL-3.0-only with the additional permission in COPYING.md.
#include "mesh/Islands.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <tuple>

namespace BetterEnchantmentEffects {
std::string_view IslandSourceName(IslandSource a_source) noexcept {
  return NameOf(kIslandSources, a_source);
}

std::string_view PlainIslandSourceName(IslandSource a_source) noexcept {
  const IslandSourceSpec *row = RowOf(kIslandSources, a_source);
  return row ? row->plainName : "?";
}

SourceKind IslandBakeOf(IslandSource a_source) noexcept {
  const IslandSourceSpec *row = RowOf(kIslandSources, a_source);
  return SourceKind{BakeSource{row ? row->bake : BakeKind{ComponentIdBake{}}}};
}

namespace {
constexpr std::size_t kMaxAnalysedVertices = std::size_t{1} << 24;
constexpr float kComponentWeldUnits = 1.0f / 1024.0f;
constexpr float kMaxWeldCell = 1099511627776.0f;

class DisjointSets {
public:
  explicit DisjointSets(std::size_t a_count)
      : parent_(a_count), size_(a_count, 1) {
    std::iota(parent_.begin(), parent_.end(), std::uint32_t{0});
  }

  [[nodiscard]] std::uint32_t Find(std::uint32_t a_index) noexcept {
    if (a_index >= parent_.size()) {
      return a_index;
    }
    while (parent_[a_index] != a_index) {
      parent_[a_index] = parent_[parent_[a_index]];
      a_index = parent_[a_index];
    }
    return a_index;
  }

  void Join(std::uint32_t a_left, std::uint32_t a_right) noexcept {
    std::uint32_t l = Find(a_left);
    std::uint32_t r = Find(a_right);
    if (l == r || l >= parent_.size() || r >= parent_.size()) {
      return;
    }
    if (size_[l] < size_[r]) {
      std::swap(l, r);
    }
    parent_[r] = l;
    size_[l] += size_[r];
  }

private:
  std::vector<std::uint32_t> parent_;
  std::vector<std::uint32_t> size_;
};

struct FlatVertex {
  const MeshVertex *vertex = nullptr;
  const MeshPartition *partition = nullptr;
};

struct FlatMesh {
  std::vector<FlatVertex> vertices;
  std::vector<std::array<std::uint32_t, 3>> triangles;
};

[[nodiscard]] std::optional<FlatMesh> Flatten(const MeshData &a_mesh) {
  std::size_t total = 0;
  for (const MeshPartition &partition : a_mesh.partitions) {
    total += partition.vertices.size();
  }
  if (total > kMaxAnalysedVertices) {
    return std::nullopt;
  }
  FlatMesh flat;
  flat.vertices.reserve(total);
  for (const MeshPartition &partition : a_mesh.partitions) {
    const std::uint32_t base = static_cast<std::uint32_t>(flat.vertices.size());
    const std::size_t count = partition.vertices.size();
    for (const MeshVertex &vertex : partition.vertices) {
      flat.vertices.push_back(FlatVertex{&vertex, &partition});
    }
    for (const std::array<std::uint32_t, 3> &triangle : partition.triangles) {
      if (triangle[0] < count && triangle[1] < count && triangle[2] < count) {
        flat.triangles.push_back(
            {base + triangle[0], base + triangle[1], base + triangle[2]});
      }
    }
  }
  return flat;
}

using WeldCell = std::tuple<std::int64_t, std::int64_t, std::int64_t>;

[[nodiscard]] std::optional<std::int64_t> CellOf(float a_value,
                                                 float a_cellSize) noexcept {
  const float scaled = a_value / a_cellSize;
  if (!std::isfinite(scaled)) {
    return std::nullopt;
  }
  return static_cast<std::int64_t>(
      std::floor(std::clamp(scaled, -kMaxWeldCell, kMaxWeldCell)));
}

[[nodiscard]] std::optional<WeldCell>
PositionCell(const MeshVertex &a_vertex) noexcept {
  const std::optional<std::int64_t> x =
      CellOf(a_vertex.position.x, kComponentWeldUnits);
  const std::optional<std::int64_t> y =
      CellOf(a_vertex.position.y, kComponentWeldUnits);
  const std::optional<std::int64_t> z =
      CellOf(a_vertex.position.z, kComponentWeldUnits);
  if (!x || !y || !z) {
    return std::nullopt;
  }
  return WeldCell{*x, *y, *z};
}

[[nodiscard]] std::optional<WeldCell>
UvCell(const MeshVertex &a_vertex) noexcept {
  const std::optional<std::int64_t> u = CellOf(a_vertex.uv.x, kChartWeldUv);
  const std::optional<std::int64_t> v = CellOf(a_vertex.uv.y, kChartWeldUv);
  if (!u || !v) {
    return std::nullopt;
  }
  return WeldCell{*u, *v, 0};
}

using CellOfVertex = std::optional<WeldCell> (*)(const MeshVertex &) noexcept;

[[nodiscard]] DisjointSets Connect(const FlatMesh &a_flat,
                                   CellOfVertex a_cellOf) {
  DisjointSets sets(a_flat.vertices.size());
  std::map<WeldCell, std::uint32_t> firstInCell;
  for (const std::array<std::uint32_t, 3> &triangle : a_flat.triangles) {
    sets.Join(triangle[0], triangle[1]);
    sets.Join(triangle[1], triangle[2]);
    for (const std::uint32_t corner : triangle) {
      if (corner >= a_flat.vertices.size() || !a_flat.vertices[corner].vertex) {
        continue;
      }
      const std::optional<WeldCell> cell =
          a_cellOf(*a_flat.vertices[corner].vertex);
      if (!cell) {
        continue;
      }
      const auto [it, inserted] = firstInCell.emplace(*cell, corner);
      if (!inserted) {
        sets.Join(corner, it->second);
      }
    }
  }
  return sets;
}

struct Labelling {
  std::vector<std::uint16_t> ofVertex;
  std::vector<MeshIsland> islands;
};

struct RootCount {
  std::uint32_t root = 0;
  std::size_t triangles = 0;
};

struct IslandTally {
  std::size_t members = 0;
  Vec3 positionSum{};
  std::map<std::string, float> boneWeight;
};

struct DominantBone {
  std::string name;
  float share = 0.0f;
};

[[nodiscard]] std::vector<RootCount> RankedRoots(const FlatMesh &a_flat,
                                                 DisjointSets &a_sets) {
  const std::size_t vertexCount = a_flat.vertices.size();
  std::vector<std::size_t> trianglesOfRoot(vertexCount, 0);
  for (const std::array<std::uint32_t, 3> &triangle : a_flat.triangles) {
    const std::uint32_t root = a_sets.Find(triangle[0]);
    if (root < vertexCount) {
      ++trianglesOfRoot[root];
    }
  }
  std::vector<RootCount> ranked;
  for (std::uint32_t root = 0; root < vertexCount; ++root) {
    if (trianglesOfRoot[root] > 0) {
      ranked.push_back(RootCount{root, trianglesOfRoot[root]});
    }
  }
  std::stable_sort(
      ranked.begin(), ranked.end(), [](const RootCount &a, const RootCount &b) {
        return a.triangles != b.triangles ? a.triangles > b.triangles
                                          : a.root < b.root;
      });
  return ranked;
}

[[nodiscard]] std::vector<std::uint16_t>
LabelVertices(std::size_t a_vertexCount, std::span<const RootCount> a_listed,
              DisjointSets &a_sets) {
  std::vector<std::uint16_t> idOfRoot(a_vertexCount, kNoIsland);
  for (std::size_t i = 0; i < a_listed.size(); ++i) {
    if (a_listed[i].root < a_vertexCount) {
      idOfRoot[a_listed[i].root] = static_cast<std::uint16_t>(i);
    }
  }
  std::vector<std::uint16_t> ofVertex(a_vertexCount, kNoIsland);
  for (std::uint32_t v = 0; v < a_vertexCount; ++v) {
    const std::uint32_t root = a_sets.Find(v);
    ofVertex[v] = root < a_vertexCount ? idOfRoot[root] : kNoIsland;
  }
  return ofVertex;
}

[[nodiscard]] std::vector<IslandTally>
TallyIslands(const FlatMesh &a_flat, std::span<const std::uint16_t> a_ofVertex,
             std::size_t a_listed) {
  std::vector<IslandTally> tallies(a_listed);
  const std::size_t vertexCount =
      (std::min)(a_flat.vertices.size(), a_ofVertex.size());
  for (std::size_t v = 0; v < vertexCount; ++v) {
    const std::uint16_t id = a_ofVertex[v];
    const FlatVertex &flat = a_flat.vertices[v];
    if (id >= tallies.size() || !flat.vertex || !flat.partition) {
      continue;
    }
    IslandTally &tally = tallies[id];
    ++tally.members;
    tally.positionSum.x += flat.vertex->position.x;
    tally.positionSum.y += flat.vertex->position.y;
    tally.positionSum.z += flat.vertex->position.z;
    AddBoneWeights(tally.boneWeight, *flat.vertex, *flat.partition);
  }
  return tallies;
}

[[nodiscard]] DominantBone
DominantBoneOf(const std::map<std::string, float> &a_weights) {
  DominantBone dominant;
  float total = 0.0f;
  float best = 0.0f;
  for (const auto &[name, weight] : a_weights) {
    total += weight;
    if (weight > best) {
      best = weight;
      dominant.name = name;
    }
  }
  dominant.share = total > 0.0f ? best / total : 0.0f;
  return dominant;
}

[[nodiscard]] MeshIsland IslandFrom(const IslandTally &a_tally,
                                    const RootCount &a_rank,
                                    std::size_t a_totalTriangles) {
  MeshIsland island;
  island.triangles = a_rank.triangles;
  island.share = a_totalTriangles > 0 ? static_cast<float>(a_rank.triangles) /
                                            static_cast<float>(a_totalTriangles)
                                      : 0.0f;
  if (a_tally.members > 0) {
    const float count = static_cast<float>(a_tally.members);
    island.centroid =
        Vec3{a_tally.positionSum.x / count, a_tally.positionSum.y / count,
             a_tally.positionSum.z / count};
  }
  DominantBone dominant = DominantBoneOf(a_tally.boneWeight);
  island.dominantBone = std::move(dominant.name);
  island.dominantShare = dominant.share;
  return island;
}

[[nodiscard]] Labelling Label(const FlatMesh &a_flat, DisjointSets &a_sets,
                              IslandSource a_source) {
  const std::vector<RootCount> ranked = RankedRoots(a_flat, a_sets);
  const std::span<const RootCount> listed =
      std::span<const RootCount>{ranked}.first(
          std::min<std::size_t>(ranked.size(), kMaxIslands));
  Labelling out;
  out.ofVertex = LabelVertices(a_flat.vertices.size(), listed, a_sets);
  const std::vector<IslandTally> tallies =
      TallyIslands(a_flat, out.ofVertex, listed.size());
  for (std::size_t i = 0; i < listed.size() && i < tallies.size(); ++i) {
    MeshIsland island =
        IslandFrom(tallies[i], listed[i], a_flat.triangles.size());
    island.source = a_source;
    island.id = static_cast<std::uint16_t>(i);
    out.islands.push_back(std::move(island));
  }
  return out;
}

[[nodiscard]] std::size_t RegionsOf(const MeshAnalysis &a_analysis,
                                    IslandSource a_source) noexcept {
  return a_source == IslandSource::kComponent ? a_analysis.components
                                              : a_analysis.charts;
}

[[nodiscard]] const std::vector<std::uint16_t> &
TableOf(const MeshAnalysis &a_analysis, IslandSource a_source) noexcept {
  return a_source == IslandSource::kComponent ? a_analysis.componentOf
                                              : a_analysis.chartOf;
}

struct ComponentChart {
  std::optional<std::uint16_t> only;
  bool several = false;
};

struct TwinCandidates {
  std::vector<ComponentChart> chartOfComponent;
  std::vector<std::size_t> componentSize;
  std::vector<std::size_t> chartSize;
};

[[nodiscard]] TwinCandidates TwinCandidatesOf(const MeshAnalysis &a_analysis) {
  TwinCandidates out{std::vector<ComponentChart>(a_analysis.components),
                     std::vector<std::size_t>(a_analysis.components, 0),
                     std::vector<std::size_t>(a_analysis.charts, 0)};
  const std::size_t vertices =
      (std::min)(a_analysis.componentOf.size(), a_analysis.chartOf.size());
  for (std::size_t v = 0; v < vertices; ++v) {
    const std::uint16_t c = a_analysis.componentOf[v];
    const std::uint16_t k = a_analysis.chartOf[v];
    if (c < out.componentSize.size()) {
      ++out.componentSize[c];
    }
    if (k < out.chartSize.size()) {
      ++out.chartSize[k];
    }
    if (c >= out.chartOfComponent.size() || k == kNoIsland) {
      continue;
    }
    ComponentChart &seen = out.chartOfComponent[c];
    if (!seen.only) {
      seen.only = k;
    } else if (*seen.only != k) {
      seen.several = true;
    }
  }
  return out;
}

[[nodiscard]] std::optional<std::uint16_t>
TwinChartOf(const TwinCandidates &a_candidates, std::uint16_t a_component) {
  if (a_component >= a_candidates.chartOfComponent.size() ||
      a_component >= a_candidates.componentSize.size()) {
    return std::nullopt;
  }
  const ComponentChart &seen = a_candidates.chartOfComponent[a_component];
  if (seen.several || !seen.only) {
    return std::nullopt;
  }
  const std::uint16_t k = *seen.only;
  if (k >= a_candidates.chartSize.size() ||
      a_candidates.chartSize[k] != a_candidates.componentSize[a_component]) {
    return std::nullopt;
  }
  return k;
}

void LinkChartTwin(std::vector<MeshIsland> &a_islands, std::uint16_t a_chart,
                   std::uint16_t a_component) {
  for (MeshIsland &chart : a_islands) {
    if (chart.source == IslandSource::kChart && chart.id == a_chart) {
      chart.twin = a_component;
    }
  }
}

void PairTwins(MeshAnalysis &a_analysis) {
  const TwinCandidates candidates = TwinCandidatesOf(a_analysis);
  for (MeshIsland &island : a_analysis.islands) {
    if (island.source != IslandSource::kComponent) {
      continue;
    }
    const std::optional<std::uint16_t> chart =
        TwinChartOf(candidates, island.id);
    if (!chart) {
      continue;
    }
    island.twin = *chart;
    LinkChartTwin(a_analysis.islands, *chart, island.id);
  }
}
}

MeshAnalysis AnalyseMesh(const MeshData &a_mesh) {
  MeshAnalysis out;
  const std::optional<FlatMesh> flat = Flatten(a_mesh);
  if (!flat) {
    return out;
  }
  DisjointSets components = Connect(*flat, &PositionCell);
  Labelling byComponent = Label(*flat, components, IslandSource::kComponent);
  DisjointSets charts = Connect(*flat, &UvCell);
  Labelling byChart = Label(*flat, charts, IslandSource::kChart);

  out.components = static_cast<std::uint16_t>(byComponent.islands.size());
  out.charts = static_cast<std::uint16_t>(byChart.islands.size());
  out.islands = std::move(byComponent.islands);
  out.islands.insert(out.islands.end(), byChart.islands.begin(),
                     byChart.islands.end());
  out.componentOf = std::move(byComponent.ofVertex);
  out.chartOf = std::move(byChart.ofVertex);
  PairTwins(out);
  return out;
}

BakeBuffers BuildIslandBake(const MeshData &a_mesh,
                            const MeshAnalysis &a_analysis,
                            IslandSource a_source) {
  BakeBuffers out;
  out.vector = false;
  if (RegionsOf(a_analysis, a_source) == 0) {
    out.problem = std::format("the analysis found no {} of the mesh",
                              IslandSourceName(a_source));
    return out;
  }
  const std::vector<std::uint16_t> &table = TableOf(a_analysis, a_source);
  std::size_t next = 0;
  for (const MeshPartition &partition : a_mesh.partitions) {
    if (next + partition.vertices.size() > table.size()) {
      out.vertices.clear();
      out.indices.clear();
      out.problem = "the analysis is of a different mesh";
      return out;
    }
    const std::uint32_t base = static_cast<std::uint32_t>(out.vertices.size());
    for (const MeshVertex &vertex : partition.vertices) {
      const std::uint16_t id = table[next++];
      BakeVertex bv;
      bv.u = vertex.uv.x;
      bv.v = vertex.uv.y;
      bv.value[0] = id == kNoIsland ? 1.0f : static_cast<float>(id) / 255.0f;
      out.vertices.push_back(bv);
    }
    for (const std::array<std::uint32_t, 3> &triangle : partition.triangles) {
      if (triangle[0] < partition.vertices.size() &&
          triangle[1] < partition.vertices.size() &&
          triangle[2] < partition.vertices.size()) {
        out.indices.push_back(base + triangle[0]);
        out.indices.push_back(base + triangle[1]);
        out.indices.push_back(base + triangle[2]);
      }
    }
  }
  if (out.indices.empty()) {
    out.vertices.clear();
    out.problem = "the mesh has no triangles to bake";
  }
  return out;
}
}
