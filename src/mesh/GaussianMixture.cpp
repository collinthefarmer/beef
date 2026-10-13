// GPL-3.0-only with the additional permission in COPYING.md.
#include "mesh/GaussianMixture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects {
namespace {
constexpr double kLogTwoPi = 1.8378770664093453;
constexpr std::size_t kKMeansIterations = 20;
constexpr std::size_t kEmIterations = 100;
constexpr double kEmTolerance = 1e-3;
constexpr double kEmptyComponent = 1e-8;
constexpr std::uint64_t kRestartStride = 7919;
constexpr std::uint64_t kComponentStride = 104729;

class SplitMix {
public:
  explicit SplitMix(std::uint64_t a_seed) noexcept : state_(a_seed) {}

  [[nodiscard]] double Unit() noexcept {
    return static_cast<double>(Next() >> 11) * 0x1.0p-53;
  }

  [[nodiscard]] std::size_t Below(std::size_t a_count) noexcept {
    if (a_count == 0) {
      return 0;
    }
    const auto pick =
        static_cast<std::size_t>(Unit() * static_cast<double>(a_count));
    return std::min(pick, a_count - 1);
  }

private:
  [[nodiscard]] std::uint64_t Next() noexcept {
    state_ += 0x9E3779B97F4A7C15ull;
    std::uint64_t z = state_;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }

  std::uint64_t state_;
};

[[nodiscard]] double SquaredDistance(std::span<const double> a_left,
                                     std::span<const double> a_right) noexcept {
  double sum = 0.0;
  const std::size_t n = std::min(a_left.size(), a_right.size());
  for (std::size_t i = 0; i < n; ++i) {
    const double d = a_left[i] - a_right[i];
    sum += d * d;
  }
  return sum;
}

[[nodiscard]] std::optional<std::vector<double>>
CholeskyLower(std::span<const double> a_covariance, std::size_t a_dimension) {
  if (a_covariance.size() != a_dimension * a_dimension) {
    return std::nullopt;
  }
  std::vector<double> lower(a_dimension * a_dimension, 0.0);
  for (std::size_t i = 0; i < a_dimension; ++i) {
    for (std::size_t j = 0; j <= i; ++j) {
      double sum = a_covariance[i * a_dimension + j];
      for (std::size_t k = 0; k < j; ++k) {
        sum -= lower[i * a_dimension + k] * lower[j * a_dimension + k];
      }
      if (i != j) {
        lower[i * a_dimension + j] = sum / lower[j * a_dimension + j];
        continue;
      }
      if (!std::isfinite(sum) || sum <= 0.0) {
        return std::nullopt;
      }
      lower[i * a_dimension + i] = std::sqrt(sum);
    }
  }
  return lower;
}

[[nodiscard]] double MahalanobisSquared(const PreparedGaussian &a_gaussian,
                                        std::span<const double> a_point) {
  const std::size_t dimension = a_gaussian.mean.size();
  std::array<double, kMaxMixtureDimension> solved{};
  double sum = 0.0;
  for (std::size_t i = 0; i < dimension; ++i) {
    double value = a_point[i] - a_gaussian.mean[i];
    for (std::size_t k = 0; k < i; ++k) {
      value -= a_gaussian.lower[i * dimension + k] * solved[k];
    }
    solved[i] = value / a_gaussian.lower[i * dimension + i];
    sum += solved[i] * solved[i];
  }
  return sum;
}

[[nodiscard]] double LogSumExp(std::span<const double> a_values) noexcept {
  double most = -std::numeric_limits<double>::infinity();
  for (const double value : a_values) {
    most = std::max(most, value);
  }
  if (!std::isfinite(most)) {
    return most;
  }
  double sum = 0.0;
  for (const double value : a_values) {
    sum += std::exp(value - most);
  }
  return most + std::log(sum);
}

[[nodiscard]] std::vector<std::size_t>
SpreadCentres(const Points &a_points, std::size_t a_count, SplitMix &a_random) {
  const std::size_t count = CountOf(a_points);
  std::vector<std::size_t> centres{a_random.Below(count)};
  std::vector<double> nearest(count, std::numeric_limits<double>::infinity());
  while (centres.size() < a_count) {
    double total = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
      nearest[i] = std::min(nearest[i],
                            SquaredDistance(PointAt(a_points, i),
                                            PointAt(a_points, centres.back())));
      total += nearest[i];
    }
    if (!(total > 0.0)) {
      centres.push_back(a_random.Below(count));
      continue;
    }
    double target = a_random.Unit() * total;
    std::size_t pick = count - 1;
    for (std::size_t i = 0; i < count; ++i) {
      target -= nearest[i];
      if (target <= 0.0) {
        pick = i;
        break;
      }
    }
    centres.push_back(pick);
  }
  return centres;
}

[[nodiscard]] std::vector<std::size_t>
KMeansLabels(const Points &a_points, std::size_t a_count, SplitMix &a_random) {
  const std::size_t count = CountOf(a_points);
  const std::size_t dimension = a_points.dimension;
  std::vector<double> centres;
  for (const std::size_t index : SpreadCentres(a_points, a_count, a_random)) {
    const std::span<const double> point = PointAt(a_points, index);
    centres.insert(centres.end(), point.begin(), point.end());
  }
  std::vector<std::size_t> labels(count, 0);
  for (std::size_t round = 0; round < kKMeansIterations; ++round) {
    for (std::size_t i = 0; i < count; ++i) {
      double best = std::numeric_limits<double>::infinity();
      for (std::size_t c = 0; c < a_count; ++c) {
        const double d = SquaredDistance(
            PointAt(a_points, i),
            std::span<const double>(centres).subspan(c * dimension, dimension));
        if (d < best) {
          best = d;
          labels[i] = c;
        }
      }
    }
    std::vector<double> sums(a_count * dimension, 0.0);
    std::vector<std::size_t> sizes(a_count, 0);
    for (std::size_t i = 0; i < count; ++i) {
      const std::span<const double> point = PointAt(a_points, i);
      for (std::size_t d = 0; d < dimension; ++d) {
        sums[labels[i] * dimension + d] += point[d];
      }
      ++sizes[labels[i]];
    }
    for (std::size_t c = 0; c < a_count; ++c) {
      if (sizes[c] == 0) {
        continue;
      }
      for (std::size_t d = 0; d < dimension; ++d) {
        centres[c * dimension + d] =
            sums[c * dimension + d] / static_cast<double>(sizes[c]);
      }
    }
  }
  return labels;
}

struct Expectation {
  std::vector<double> responsibilities;
  double logLikelihood = 0.0;
};

[[nodiscard]] std::expected<Expectation, std::string>
Expect(const Points &a_points, const std::vector<Gaussian> &a_components) {
  std::vector<PreparedGaussian> prepared;
  for (const Gaussian &component : a_components) {
    auto ready = Prepare(component);
    if (!ready) {
      return std::unexpected(ready.error());
    }
    prepared.push_back(std::move(*ready));
  }
  const std::size_t count = CountOf(a_points);
  const std::size_t k = prepared.size();
  Expectation out;
  out.responsibilities.resize(count * k);
  std::vector<double> logs(k);
  for (std::size_t i = 0; i < count; ++i) {
    for (std::size_t c = 0; c < k; ++c) {
      logs[c] = LogDensity(prepared[c], PointAt(a_points, i));
    }
    const double total = LogSumExp(logs);
    if (!std::isfinite(total)) {
      return std::unexpected("a point has no finite density");
    }
    out.logLikelihood += total;
    for (std::size_t c = 0; c < k; ++c) {
      out.responsibilities[i * k + c] = std::exp(logs[c] - total);
    }
  }
  return out;
}

[[nodiscard]] std::expected<std::vector<Gaussian>, std::string>
Maximise(const Points &a_points, const std::vector<double> &a_responsibilities,
         std::size_t a_components, double a_regularisation) {
  const std::size_t count = CountOf(a_points);
  const std::size_t dimension = a_points.dimension;
  std::vector<Gaussian> out;
  for (std::size_t c = 0; c < a_components; ++c) {
    double mass = 0.0;
    std::vector<double> mean(dimension, 0.0);
    for (std::size_t i = 0; i < count; ++i) {
      const double r = a_responsibilities[i * a_components + c];
      mass += r;
      const std::span<const double> point = PointAt(a_points, i);
      for (std::size_t d = 0; d < dimension; ++d) {
        mean[d] += r * point[d];
      }
    }
    if (mass < kEmptyComponent) {
      return std::unexpected(std::format("component {} emptied", c));
    }
    for (double &value : mean) {
      value /= mass;
    }
    std::vector<double> covariance(dimension * dimension, 0.0);
    for (std::size_t i = 0; i < count; ++i) {
      const double r = a_responsibilities[i * a_components + c];
      const std::span<const double> point = PointAt(a_points, i);
      for (std::size_t a = 0; a < dimension; ++a) {
        for (std::size_t b = 0; b <= a; ++b) {
          covariance[a * dimension + b] +=
              r * (point[a] - mean[a]) * (point[b] - mean[b]);
        }
      }
    }
    for (std::size_t a = 0; a < dimension; ++a) {
      for (std::size_t b = 0; b <= a; ++b) {
        const double value = covariance[a * dimension + b] / mass +
                             (a == b ? a_regularisation : 0.0);
        covariance[a * dimension + b] = value;
        covariance[b * dimension + a] = value;
      }
    }
    out.push_back(Gaussian{std::move(mean), std::move(covariance),
                           mass / static_cast<double>(count)});
  }
  return out;
}

[[nodiscard]] std::vector<double>
HardResponsibilities(const std::vector<std::size_t> &a_labels,
                     std::size_t a_components) {
  std::vector<double> out(a_labels.size() * a_components, 0.0);
  for (std::size_t i = 0; i < a_labels.size(); ++i) {
    out[i * a_components + a_labels[i]] = 1.0;
  }
  return out;
}

[[nodiscard]] std::expected<Mixture, std::string>
FitFrom(const Points &a_points, std::vector<double> a_responsibilities,
        std::size_t a_components, double a_regularisation) {
  const double count = static_cast<double>(CountOf(a_points));
  double previous = -std::numeric_limits<double>::infinity();
  Mixture mixture{a_points.dimension, {}, previous};
  for (std::size_t round = 0; round < kEmIterations; ++round) {
    auto components =
        Maximise(a_points, a_responsibilities, a_components, a_regularisation);
    if (!components) {
      return std::unexpected(components.error());
    }
    auto expectation = Expect(a_points, *components);
    if (!expectation) {
      return std::unexpected(expectation.error());
    }
    mixture.components = std::move(*components);
    mixture.logLikelihood = expectation->logLikelihood;
    a_responsibilities = std::move(expectation->responsibilities);
    if (std::abs(mixture.logLikelihood - previous) / count < kEmTolerance) {
      break;
    }
    previous = mixture.logLikelihood;
  }
  return mixture;
}
}

std::size_t CountOf(const Points &a_points) noexcept {
  return a_points.dimension == 0 ? 0
                                 : a_points.values.size() / a_points.dimension;
}

std::span<const double> PointAt(const Points &a_points,
                                std::size_t a_index) noexcept {
  if (a_index >= CountOf(a_points)) {
    return {};
  }
  return std::span<const double>(a_points.values)
      .subspan(a_index * a_points.dimension, a_points.dimension);
}

std::expected<Points, std::string> PointsOf(std::size_t a_dimension,
                                            std::vector<double> a_values) {
  if (a_dimension == 0 || a_dimension > kMaxMixtureDimension) {
    return std::unexpected(std::format("dimension {} is outside 1..{}",
                                       a_dimension, kMaxMixtureDimension));
  }
  if (a_values.size() % a_dimension != 0) {
    return std::unexpected("the values do not fill whole points");
  }
  if (a_values.size() / a_dimension > kMaxMixturePoints) {
    return std::unexpected(
        std::format("more than {} points", kMaxMixturePoints));
  }
  if (!std::ranges::all_of(
          a_values, [](double a_value) { return std::isfinite(a_value); })) {
    return std::unexpected("a value is not finite");
  }
  return Points{a_dimension, std::move(a_values)};
}

std::expected<Gaussian, std::string>
GaussianOf(const Points &a_points, std::span<const std::size_t> a_members,
           double a_weight, double a_regularisation) {
  const std::size_t dimension = a_points.dimension;
  const std::size_t count = CountOf(a_points);
  if (a_members.empty()) {
    return std::unexpected("a Gaussian needs at least one member");
  }
  if (std::ranges::any_of(a_members, [count](std::size_t a_index) {
        return a_index >= count;
      })) {
    return std::unexpected("a member is outside the points");
  }
  std::vector<double> mean(dimension, 0.0);
  for (const std::size_t index : a_members) {
    const std::span<const double> point = PointAt(a_points, index);
    for (std::size_t d = 0; d < dimension; ++d) {
      mean[d] += point[d];
    }
  }
  const double n = static_cast<double>(a_members.size());
  for (double &value : mean) {
    value /= n;
  }
  std::vector<double> covariance(dimension * dimension, 0.0);
  for (const std::size_t index : a_members) {
    const std::span<const double> point = PointAt(a_points, index);
    for (std::size_t a = 0; a < dimension; ++a) {
      for (std::size_t b = 0; b < dimension; ++b) {
        covariance[a * dimension + b] +=
            (point[a] - mean[a]) * (point[b] - mean[b]);
      }
    }
  }
  const double divisor = n > 1.0 ? n - 1.0 : 1.0;
  for (std::size_t a = 0; a < dimension; ++a) {
    for (std::size_t b = 0; b < dimension; ++b) {
      covariance[a * dimension + b] = covariance[a * dimension + b] / divisor +
                                      (a == b ? a_regularisation : 0.0);
    }
  }
  return Gaussian{std::move(mean), std::move(covariance), a_weight};
}

std::expected<PreparedGaussian, std::string>
Prepare(const Gaussian &a_gaussian) {
  const std::size_t dimension = a_gaussian.mean.size();
  if (dimension == 0 || dimension > kMaxMixtureDimension) {
    return std::unexpected("a Gaussian has no usable dimension");
  }
  if (!(a_gaussian.weight > 0.0) || !std::isfinite(a_gaussian.weight)) {
    return std::unexpected("a Gaussian has no weight");
  }
  auto lower = CholeskyLower(a_gaussian.covariance, dimension);
  if (!lower) {
    return std::unexpected("a covariance is not positive definite");
  }
  double logDeterminantHalf = 0.0;
  for (std::size_t i = 0; i < dimension; ++i) {
    logDeterminantHalf += std::log((*lower)[i * dimension + i]);
  }
  const double logScale = std::log(a_gaussian.weight) - logDeterminantHalf -
                          0.5 * static_cast<double>(dimension) * kLogTwoPi;
  return PreparedGaussian{a_gaussian.mean, std::move(*lower), logScale};
}

double LogDensity(const PreparedGaussian &a_gaussian,
                  std::span<const double> a_point) noexcept {
  const std::size_t dimension = a_gaussian.mean.size();
  if (a_point.size() != dimension || dimension > kMaxMixtureDimension ||
      a_gaussian.lower.size() != dimension * dimension) {
    return -std::numeric_limits<double>::infinity();
  }
  return a_gaussian.logScale - 0.5 * MahalanobisSquared(a_gaussian, a_point);
}

std::vector<double> Posteriors(std::span<const PreparedGaussian> a_gaussians,
                               std::span<const double> a_point) {
  std::vector<double> logs;
  logs.reserve(a_gaussians.size());
  for (const PreparedGaussian &gaussian : a_gaussians) {
    logs.push_back(LogDensity(gaussian, a_point));
  }
  const double total = LogSumExp(logs);
  for (double &value : logs) {
    value = std::isfinite(total) ? std::exp(value - total) : 0.0;
  }
  return logs;
}

std::expected<Mixture, std::string> FitMixture(const Points &a_points,
                                               std::size_t a_components,
                                               std::uint32_t a_seed,
                                               double a_regularisation) {
  if (a_components == 0 || a_components > kMaxMixtureComponents) {
    return std::unexpected(std::format("{} components is outside 1..{}",
                                       a_components, kMaxMixtureComponents));
  }
  if (CountOf(a_points) < 2 * a_components) {
    return std::unexpected(std::format("{} points cannot hold {} components",
                                       CountOf(a_points), a_components));
  }
  SplitMix random(a_seed);
  const std::vector<std::size_t> labels =
      KMeansLabels(a_points, a_components, random);
  return FitFrom(a_points, HardResponsibilities(labels, a_components),
                 a_components, a_regularisation);
}

double InformationCriterion(const Mixture &a_mixture,
                            std::size_t a_points) noexcept {
  const double d = static_cast<double>(a_mixture.dimension);
  const double k = static_cast<double>(a_mixture.components.size());
  const double parameters = k * d + k * d * (d + 1.0) / 2.0 + (k - 1.0);
  return -2.0 * a_mixture.logLikelihood +
         parameters * std::log(static_cast<double>(a_points));
}

double CompletedCriterion(const Mixture &a_mixture, const Points &a_points) {
  std::vector<PreparedGaussian> prepared;
  for (const Gaussian &component : a_mixture.components) {
    auto ready = Prepare(component);
    if (!ready) {
      return std::numeric_limits<double>::infinity();
    }
    prepared.push_back(std::move(*ready));
  }
  double entropy = 0.0;
  for (std::size_t i = 0; i < CountOf(a_points); ++i) {
    for (const double p : Posteriors(prepared, PointAt(a_points, i))) {
      if (p > 0.0) {
        entropy -= p * std::log(p);
      }
    }
  }
  return InformationCriterion(a_mixture, CountOf(a_points)) + 2.0 * entropy;
}

std::expected<Mixture, std::string>
ChooseMixture(const Points &a_points, const MixtureSearch &a_search) {
  const std::size_t count = CountOf(a_points);
  const std::size_t most =
      std::min({a_search.most, kMaxMixtureComponents, count / 2});
  std::optional<std::pair<double, Mixture>> best;
  std::string lastProblem = "no component count fits";
  for (std::size_t k = std::max<std::size_t>(a_search.fewest, 1); k <= most;
       ++k) {
    std::optional<Mixture> likeliest;
    for (std::size_t restart = 0;
         restart < std::max<std::size_t>(a_search.restarts, 1); ++restart) {
      const auto seed = static_cast<std::uint32_t>(
          a_search.seed + restart * kRestartStride + k * kComponentStride);
      auto fitted = FitMixture(a_points, k, seed, a_search.regularisation);
      if (!fitted) {
        lastProblem = fitted.error();
        continue;
      }
      if (!likeliest || fitted->logLikelihood > likeliest->logLikelihood) {
        likeliest = std::move(*fitted);
      }
    }
    if (!likeliest) {
      continue;
    }
    const double score = CompletedCriterion(*likeliest, a_points);
    if (!best || score < best->first) {
      best.emplace(score, std::move(*likeliest));
    }
  }
  if (!best) {
    return std::unexpected(lastProblem);
  }
  return std::move(best->second);
}

std::vector<std::size_t> MostLikelyComponents(const Mixture &a_mixture,
                                              const Points &a_points) {
  std::vector<std::optional<PreparedGaussian>> prepared;
  for (const Gaussian &component : a_mixture.components) {
    auto ready = Prepare(component);
    prepared.push_back(ready ? std::optional(std::move(*ready)) : std::nullopt);
  }
  std::vector<std::size_t> out(CountOf(a_points), 0);
  for (std::size_t i = 0; i < out.size(); ++i) {
    double best = -std::numeric_limits<double>::infinity();
    for (std::size_t c = 0; c < prepared.size(); ++c) {
      if (!prepared[c]) {
        continue;
      }
      const double value = LogDensity(*prepared[c], PointAt(a_points, i));
      if (value > best) {
        best = value;
        out[i] = c;
      }
    }
  }
  return out;
}
}
