// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects {
inline constexpr std::size_t kMaxMixtureDimension = 12;
inline constexpr std::size_t kMaxMixtureComponents = 8;
inline constexpr std::size_t kMaxMixturePoints = 1u << 20;

struct Points {
  std::size_t dimension = 0;
  std::vector<double> values;
};

struct Gaussian {
  std::vector<double> mean;
  std::vector<double> covariance;
  double weight = 0.0;
};

struct Mixture {
  std::size_t dimension = 0;
  std::vector<Gaussian> components;
  double logLikelihood = 0.0;
};

struct MixtureSearch {
  std::size_t fewest = 2;
  std::size_t most = kMaxMixtureComponents;
  std::uint32_t seed = 1;
  std::size_t restarts = 5;
  double regularisation = 1e-2;
};

struct PreparedGaussian {
  std::vector<double> mean;
  std::vector<double> lower;
  double logScale = 0.0;
};

[[nodiscard]] std::size_t CountOf(const Points &a_points) noexcept;
[[nodiscard]] std::span<const double> PointAt(const Points &a_points,
                                              std::size_t a_index) noexcept;
[[nodiscard]] std::expected<Points, std::string>
PointsOf(std::size_t a_dimension, std::vector<double> a_values);

[[nodiscard]] std::expected<Gaussian, std::string>
GaussianOf(const Points &a_points, std::span<const std::size_t> a_members,
           double a_weight, double a_regularisation);
[[nodiscard]] std::expected<PreparedGaussian, std::string>
Prepare(const Gaussian &a_gaussian);
[[nodiscard]] double LogDensity(const PreparedGaussian &a_gaussian,
                                std::span<const double> a_point) noexcept;
[[nodiscard]] std::vector<double>
Posteriors(std::span<const PreparedGaussian> a_gaussians,
           std::span<const double> a_point);

[[nodiscard]] std::expected<Mixture, std::string>
FitMixture(const Points &a_points, std::size_t a_components,
           std::uint32_t a_seed, double a_regularisation);
[[nodiscard]] double InformationCriterion(const Mixture &a_mixture,
                                          std::size_t a_points) noexcept;
[[nodiscard]] double CompletedCriterion(const Mixture &a_mixture,
                                        const Points &a_points);
[[nodiscard]] std::expected<Mixture, std::string>
ChooseMixture(const Points &a_points, const MixtureSearch &a_search);
[[nodiscard]] std::vector<std::size_t>
MostLikelyComponents(const Mixture &a_mixture, const Points &a_points);
}
