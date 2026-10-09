// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "Core.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace BetterEnchantmentEffects {
class RecipeGraph;

template <class T> struct StrongRef {
  using type = std::shared_ptr<T>;
};
template <class T> using StrongRefOf = typename StrongRef<T>::type;

template <class T> class Lease;

template <class T> class Owned {
public:
  Owned() = default;
  explicit Owned(StrongRefOf<T> a_ref) : ref_(std::move(a_ref)) {}
  Owned(const Owned &) = delete;
  Owned &operator=(const Owned &) = delete;
  Owned(Owned &&) noexcept = default;
  Owned &operator=(Owned &&) noexcept = default;
  ~Owned() = default;

  [[nodiscard]] T *Get() const noexcept { return ref_.get(); }
  [[nodiscard]] explicit operator bool() const noexcept {
    return Get() != nullptr;
  }
  [[nodiscard]] Lease<T> Grant() const { return Lease<T>{ref_}; }

private:
  StrongRefOf<T> ref_;
};

template <class T> class Lease {
public:
  Lease() = default;
  Lease(const Lease &) = delete;
  Lease &operator=(const Lease &) = delete;
  Lease(Lease &&) noexcept = default;
  Lease &operator=(Lease &&) noexcept = default;
  ~Lease() = default;

  [[nodiscard]] T *Get() const noexcept { return ref_.get(); }
  [[nodiscard]] explicit operator bool() const noexcept {
    return Get() != nullptr;
  }

private:
  friend class Owned<T>;
  explicit Lease(StrongRefOf<T> a_ref) : ref_(std::move(a_ref)) {}

  StrongRefOf<T> ref_;
};

[[nodiscard]] constexpr std::size_t CombineHash(std::size_t a_seed,
                                                std::size_t a_value) noexcept {
  return a_seed ^
         (a_value + 0x9e3779b97f4a7c15ULL + (a_seed << 6) + (a_seed >> 2));
}

struct TargetAddress {
  std::uintptr_t address = 0;
  std::uint64_t generation = 0;

  [[nodiscard]] std::strong_ordering
  operator<=>(const TargetAddress &) const = default;
  [[nodiscard]] bool operator==(const TargetAddress &) const = default;
  [[nodiscard]] std::size_t Hash() const noexcept {
    return CombineHash(std::hash<std::uintptr_t>{}(address),
                       std::hash<std::uint64_t>{}(generation));
  }
};

struct NamedTexture {
  std::string name;
  std::uint32_t width = 0;
  std::uint32_t height = 0;

  [[nodiscard]] std::strong_ordering
  operator<=>(const NamedTexture &) const = default;
  [[nodiscard]] bool operator==(const NamedTexture &) const = default;
  [[nodiscard]] std::size_t Hash() const noexcept {
    return CombineHash(CombineHash(std::hash<std::string>{}(name), width),
                       height);
  }
};

struct MeshInstall {
  std::string mesh;
  std::uint64_t install = 0;

  [[nodiscard]] std::strong_ordering
  operator<=>(const MeshInstall &) const = default;
  [[nodiscard]] bool operator==(const MeshInstall &) const = default;
  [[nodiscard]] std::size_t Hash() const noexcept {
    return CombineHash(std::hash<std::string>{}(mesh),
                       std::hash<std::uint64_t>{}(install));
  }
};

struct GraphVersion {
  std::string recipe;
  std::uint64_t revision = 0;
  std::optional<std::string> variant;

  [[nodiscard]] std::strong_ordering
  operator<=>(const GraphVersion &) const = default;
  [[nodiscard]] bool operator==(const GraphVersion &) const = default;
  [[nodiscard]] std::size_t Hash() const noexcept {
    return CombineHash(CombineHash(std::hash<std::string>{}(recipe),
                                   std::hash<std::uint64_t>{}(revision)),
                       std::hash<std::optional<std::string>>{}(variant));
  }
};

template <class T> struct IdentityShape {
  using type = TargetAddress;
};
template <> struct IdentityShape<RecipeGraph> {
  using type = GraphVersion;
};
template <class T> using IdentityShapeOf = typename IdentityShape<T>::type;

template <class T> class Identity {
public:
  explicit Identity(IdentityShapeOf<T> a_shape) : shape_(std::move(a_shape)) {}

  [[nodiscard]] const IdentityShapeOf<T> &Shape() const noexcept {
    return shape_;
  }
  [[nodiscard]] std::strong_ordering
  operator<=>(const Identity &) const = default;
  [[nodiscard]] bool operator==(const Identity &) const = default;

private:
  IdentityShapeOf<T> shape_;
};

[[nodiscard]] inline std::optional<NamedTexture>
NamedTextureOf(std::string_view a_name, std::uint32_t a_width,
               std::uint32_t a_height) {
  if (a_name.empty()) {
    return std::nullopt;
  }
  return NamedTexture{Lower(a_name), a_width, a_height};
}
}

template <class T> struct std::hash<BetterEnchantmentEffects::Identity<T>> {
  [[nodiscard]] std::size_t operator()(
      const BetterEnchantmentEffects::Identity<T> &a_identity) const noexcept {
    return a_identity.Shape().Hash();
  }
};
