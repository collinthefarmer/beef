// GPL-3.0-only with the additional permission in COPYING.md.
#include "mesh/GaussianMixture.h"
#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Equal;

namespace {
class Noise {
public:
  explicit Noise(std::uint32_t a_seed) : state_(a_seed) {}

  double Normal() {
    const double u = Unit();
    const double v = Unit();
    return std::sqrt(-2.0 * std::log(u)) * std::cos(6.283185307179586 * v);
  }

private:
  double Unit() {
    state_ = state_ * 1664525u + 1013904223u;
    return (static_cast<double>(state_ >> 8) + 0.5) / 16777216.0;
  }
  std::uint32_t state_;
};

struct Blob {
  double x = 0.0;
  double y = 0.0;
  std::size_t count = 0;
};

Points BlobPoints(const std::vector<Blob> &a_blobs,
                  std::vector<std::size_t> &a_truth) {
  Noise noise(7);
  std::vector<double> values;
  for (std::size_t b = 0; b < a_blobs.size(); ++b) {
    for (std::size_t i = 0; i < a_blobs[b].count; ++i) {
      values.push_back(a_blobs[b].x + 0.3 * noise.Normal());
      values.push_back(a_blobs[b].y + 0.3 * noise.Normal());
      a_truth.push_back(b);
    }
  }
  return *PointsOf(2, std::move(values));
}

double Purity(const std::vector<std::size_t> &a_found,
              const std::vector<std::size_t> &a_truth, std::size_t a_groups) {
  std::size_t agree = 0;
  for (std::size_t g = 0; g < a_groups; ++g) {
    std::vector<std::size_t> votes(a_groups + 8, 0);
    for (std::size_t i = 0; i < a_found.size(); ++i) {
      if (a_truth[i] == g && a_found[i] < votes.size()) {
        ++votes[a_found[i]];
      }
    }
    agree += *std::max_element(votes.begin(), votes.end());
  }
  return static_cast<double>(agree) / static_cast<double>(a_found.size());
}
}

int main() {
  Check(!PointsOf(0, {1.0}), "a zero dimension is refused");
  Check(!PointsOf(kMaxMixtureDimension + 1,
                  std::vector<double>(kMaxMixtureDimension + 1, 0.0)),
        "a dimension past the bound is refused");
  Check(!PointsOf(2, {1.0, 2.0, 3.0}),
        "values that do not fill points are refused");
  Check(!PointsOf(1, {std::numeric_limits<double>::quiet_NaN()}),
        "a NaN value is refused");
  Check(PointAt(*PointsOf(2, {1.0, 2.0}), 5).empty(),
        "a point past the end reads as empty");

  Check(!Prepare(Gaussian{{0.0, 0.0}, {1.0, 2.0, 2.0, 1.0}, 1.0}),
        "a covariance that is not positive definite is refused");
  Check(!Prepare(Gaussian{{0.0}, {1.0}, 0.0}), "a zero weight is refused");
  const auto unit = Prepare(Gaussian{{0.0, 0.0}, {1.0, 0.0, 0.0, 1.0}, 1.0});
  Check(unit.has_value(), "a unit Gaussian prepares");
  const std::vector<double> origin{0.0, 0.0};
  test::Near(static_cast<float>(LogDensity(*unit, origin)),
             static_cast<float>(-std::log(2.0 * 3.141592653589793)),
             "the unit density at its centre", 1e-5f);
  const std::vector<double> wrongSize{0.0};
  Check(std::isinf(LogDensity(*unit, wrongSize)),
        "a point of the wrong size has no density");

  std::vector<std::size_t> truth;
  const Points two = BlobPoints({{0, 0, 200}, {4, 4, 200}}, truth);
  Check(!FitMixture(two, 0, 1, 1e-2), "zero components is refused");
  Check(!FitMixture(*PointsOf(2, {0, 0, 1, 1}), 2, 1, 1e-2),
        "too few points for the components is refused");

  const auto chosen = ChooseMixture(two, MixtureSearch{});
  Check(chosen.has_value(), "two blobs fit");
  Equal(chosen->components.size(), std::size_t{2},
        "two blobs give two components");
  Check(Purity(MostLikelyComponents(*chosen, two), truth, 2) > 0.99,
        "each blob lands in one component");

  const std::vector<PreparedGaussian> prepared = [&] {
    std::vector<PreparedGaussian> out;
    for (const Gaussian &g : chosen->components) {
      out.push_back(*Prepare(g));
    }
    return out;
  }();
  const std::vector<double> p = Posteriors(prepared, PointAt(two, 0));
  test::Near(static_cast<float>(std::accumulate(p.begin(), p.end(), 0.0)), 1.0f,
             "posteriors sum to one", 1e-5f);

  truth.clear();
  const Points three =
      BlobPoints({{0, 0, 150}, {5, 0, 150}, {0, 5, 150}}, truth);
  const auto three_fit = ChooseMixture(three, MixtureSearch{});
  Equal(three_fit->components.size(), std::size_t{3},
        "three blobs give three components");
  Check(Purity(MostLikelyComponents(*three_fit, three), truth, 3) > 0.99,
        "each of three blobs lands in one component");

  const auto again = ChooseMixture(three, MixtureSearch{});
  Check(again->logLikelihood == three_fit->logLikelihood,
        "the same seed gives the same fit");

  return test::Finish("gaussianmixture");
}
