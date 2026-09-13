#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace BetterEnchantmentEffects::Studio {
enum class FileAction : std::uint8_t { kSave, kRevert, kCount };
inline constexpr std::array<std::string_view, 2> kFileActionNames{"Save",
                                                                  "Revert"};
static_assert(kFileActionNames.size() ==
              static_cast<std::size_t>(FileAction::kCount));

enum class FileOperationState : std::uint8_t {
  kPending,
  kSucceeded,
  kFailed,
  kCount
};
inline constexpr std::array<std::string_view, 3> kFileOperationStateNames{
    "Pending", "Succeeded", "Failed"};
static_assert(kFileOperationStateNames.size() ==
              static_cast<std::size_t>(FileOperationState::kCount));

struct FileOperationResult {
  std::uint64_t requestID = 0;
  std::string recipeID;
  FileAction action = FileAction::kSave;
  FileOperationState state = FileOperationState::kPending;
  std::string path;
  std::string error;
};
}
