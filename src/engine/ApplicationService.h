#pragma once

#include "engine/SessionQueue.h"
#include "studio/ApplicationRecord.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace BetterEnchantmentEffects {
inline constexpr std::size_t kMaxTerminalApplicationActors = 256;
inline constexpr std::size_t kMaxTerminalApplicationRecipes = 256;

class ApplicationService {
public:
  using Submit = SessionQueue::Submit;
  using ApplyActor =
      std::function<void(std::uint32_t, const std::vector<ApplicationToken> &)>;

  ApplicationService();
  ApplicationService(Submit a_submit, ApplyActor a_apply);
  ~ApplicationService();
  ApplicationService(const ApplicationService &) = delete;
  ApplicationService &operator=(const ApplicationService &) = delete;

  void Post(SessionQueue::Task a_task);
  bool Refresh(std::uint32_t a_actorID);
  void Equip(std::uint32_t a_actorID, std::uint32_t a_nowMS);
  void FinalizeDue(std::uint32_t a_nowMS);
  [[nodiscard]] bool TakeEquipped(std::uint32_t a_actorID);
  void BeginLoad();
  void Resume();
  [[nodiscard]] bool Loading() const;
  [[nodiscard]] ApplicationToken Begin(std::string a_recipeID,
                                       std::vector<std::uint32_t> a_actors);
  [[nodiscard]] std::vector<std::uint32_t>
  ActorsFor(std::string_view a_recipeID) const;
  [[nodiscard]] std::vector<ApplicationToken>
  PendingFor(std::uint32_t a_actorID) const;
  void Report(const ApplicationToken &a_token, std::uint32_t a_actorID,
              ApplicationPhase a_phase, std::string a_problem = {});
  void Retire(std::uint32_t a_actorID,
              std::span<const ApplicationToken> a_tokens);
  void Cancel();
  [[nodiscard]] std::vector<ApplicationRecord> Snapshot() const;

private:
  struct Rejections;
  [[nodiscard]] ApplicationToken BeginActor(std::uint32_t a_actorID);
  void InvalidateActorScopes(std::uint32_t a_actorID, std::uint64_t a_attempt);
  void RunActor(std::uint32_t a_actorID);
  void DrainRejections();
  void PruneActors();
  void PruneRecipes();
  [[nodiscard]] ApplicationRecord *Find(const ApplicationToken &a_token);

  std::uint64_t nextRevision_ = 1;
  std::map<std::string, ApplicationRecord, std::less<>> records_;
  std::map<std::uint32_t, ApplicationRecord> actors_;
  ApplyActor apply_;
  std::shared_ptr<Rejections> rejections_;
  std::unique_ptr<SessionQueue> queue_;
};
}
