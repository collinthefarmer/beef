#pragma once

#include <optional>
#include <utility>

namespace BetterEnchantmentEffects {
template <class State> class OwnedState {
public:
  explicit OwnedState(State a_original)
      : original_(a_original), written_(std::move(a_original)) {}
  [[nodiscard]] bool Owns(const State &a_current) const {
    return a_current == written_;
  }
  void Written(State a_state) { written_ = std::move(a_state); }
  [[nodiscard]] const State &Original() const { return original_; }
  [[nodiscard]] const State &LastWritten() const { return written_; }
  [[nodiscard]] std::optional<State> Restore(const State &a_current) const {
    return Owns(a_current) ? std::optional<State>{original_} : std::nullopt;
  }

private:
  State original_;
  State written_;
};
}
