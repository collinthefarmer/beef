// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace RE {
struct PlayerCharacter {};
inline std::array<std::uintptr_t, 1> VTABLE_PlayerCharacter{};
}

namespace HookFixture {
inline std::size_t patches = 0;
inline std::size_t slot = 0;
inline std::uintptr_t original = 0;
inline std::uintptr_t replacement = 0;
inline std::vector<int> calls;
inline std::vector<std::string> errors;
}

namespace logger {
template <class... Args> void info(const char *, Args &&...) {}
inline void error(const char *, std::string_view a_message) {
  HookFixture::errors.emplace_back(a_message);
}
}

namespace REL {
template <class T> class Relocation {
public:
  Relocation() = default;
  explicit Relocation(std::uintptr_t a_address) : address_(a_address) {}
  Relocation &operator=(std::uintptr_t a_address) {
    address_ = a_address;
    return *this;
  }
  [[nodiscard]] std::uintptr_t address() const { return address_; }
  template <class F>
  std::uintptr_t write_vfunc(std::size_t a_index, F a_function) {
    ++HookFixture::patches;
    HookFixture::slot = a_index;
    HookFixture::replacement = reinterpret_cast<std::uintptr_t>(a_function);
    return HookFixture::original;
  }
  void operator()(RE::PlayerCharacter *a_actor, float a_delta) const {
    reinterpret_cast<std::add_pointer_t<T>>(address_)(a_actor, a_delta);
  }

private:
  std::uintptr_t address_ = 0;
};
}
