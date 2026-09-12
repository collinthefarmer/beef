#include "engine/ApplicationService.h"

#include <algorithm>
#include <array>
#include <mutex>
#include <utility>

namespace BetterEnchantmentEffects {
struct ApplicationService::Rejections {
  std::mutex lock;
  std::vector<std::uint32_t> actors;
};

namespace {
constexpr std::array<std::string_view, 6> kPhaseNames{
    "queued", "prepared", "rendered", "failed", "unmatched", "cancelled"};
static_assert(kPhaseNames.size() ==
              static_cast<std::size_t>(ApplicationPhase::kCancelled) + 1);

bool Pending(ApplicationPhase a_phase) {
  return a_phase == ApplicationPhase::kQueued ||
         a_phase == ApplicationPhase::kPrepared;
}

void CancelRecord(ApplicationRecord &a_record) {
  a_record.phase = ApplicationPhase::kCancelled;
  for (ApplicationActor &actor : a_record.actors) {
    actor.phase = ApplicationPhase::kCancelled;
  }
}

void AppendActors(const ApplicationRecord &a_record,
                  std::vector<std::uint32_t> &a_actors) {
  if (a_record.phase == ApplicationPhase::kCancelled) {
    return;
  }
  for (const ApplicationActor &actor : a_record.actors) {
    a_actors.push_back(actor.actorID);
  }
}

void Aggregate(ApplicationRecord &a_record) {
  a_record.problem.clear();
  if (a_record.actors.empty()) {
    a_record.phase = ApplicationPhase::kUnmatched;
    return;
  }
  constexpr std::array priority{
      ApplicationPhase::kFailed,    ApplicationPhase::kQueued,
      ApplicationPhase::kPrepared,  ApplicationPhase::kCancelled,
      ApplicationPhase::kUnmatched, ApplicationPhase::kRendered};
  for (const ApplicationPhase phase : priority) {
    const auto actor =
        std::ranges::find(a_record.actors, phase, &ApplicationActor::phase);
    if (actor != a_record.actors.end()) {
      a_record.phase = phase;
      a_record.problem = actor->problem;
      return;
    }
  }
}
}

ApplicationService::ApplicationService() : ApplicationService({}, {}) {}

ApplicationService::ApplicationService(Submit a_submit, ApplyActor a_apply)
    : apply_(std::move(a_apply)), rejections_(std::make_shared<Rejections>()),
      queue_(std::make_unique<SessionQueue>(
          std::move(a_submit),
          [this](std::uint32_t a_actorID) { RunActor(a_actorID); },
          [rejections = rejections_](std::uint32_t a_actorID) {
            const std::lock_guard lock{rejections->lock};
            rejections->actors.push_back(a_actorID);
          })) {}

ApplicationService::~ApplicationService() = default;

void ApplicationService::Post(SessionQueue::Task a_task) {
  queue_->Post(std::move(a_task));
}

bool ApplicationService::Refresh(std::uint32_t a_actorID) {
  return queue_->Refresh(a_actorID);
}

void ApplicationService::Equip(std::uint32_t a_actorID, std::uint32_t a_nowMS) {
  queue_->Equip(a_actorID, a_nowMS);
}

void ApplicationService::FinalizeDue(std::uint32_t a_nowMS) {
  DrainRejections();
  queue_->FinalizeDue(a_nowMS);
  DrainRejections();
}

bool ApplicationService::TakeEquipped(std::uint32_t a_actorID) {
  return queue_->TakeEquipped(a_actorID);
}

void ApplicationService::BeginLoad() {
  queue_->BeginLoad();
  Cancel();
}

void ApplicationService::Resume() {
  std::erase_if(actors_, [](const auto &a_actor) {
    return a_actor.second.phase == ApplicationPhase::kCancelled;
  });
  queue_->Resume();
}

bool ApplicationService::Loading() const { return queue_->Loading(); }

ApplicationToken ApplicationService::BeginActor(std::uint32_t a_actorID) {
  ApplicationRecord record;
  record.token = ApplicationToken{{}, nextRevision_++, a_actorID};
  record.actors.push_back(
      ApplicationActor{a_actorID, ApplicationPhase::kQueued, {}});
  const ApplicationToken token = record.token;
  InvalidateActorScopes(a_actorID, token.revision);
  actors_.insert_or_assign(a_actorID, std::move(record));
  return token;
}

void ApplicationService::InvalidateActorScopes(std::uint32_t a_actorID,
                                               std::uint64_t a_attempt) {
  for (auto &[recipeID, scoped] : records_) {
    const auto actor =
        std::ranges::find(scoped.actors, a_actorID, &ApplicationActor::actorID);
    if (actor != scoped.actors.end() && Pending(actor->phase)) {
      actor->attempt = a_attempt;
      actor->phase = ApplicationPhase::kQueued;
      actor->problem.clear();
      Aggregate(scoped);
    }
  }
}

void ApplicationService::RunActor(std::uint32_t a_actorID) {
  DrainRejections();
  (void)BeginActor(a_actorID);
  const auto tokens = PendingFor(a_actorID);
  if (apply_) {
    apply_(a_actorID, tokens);
  } else {
    for (const ApplicationToken &pending : tokens) {
      Report(pending, a_actorID, ApplicationPhase::kFailed,
             "actor adapter is unavailable");
    }
  }
}

void ApplicationService::DrainRejections() {
  std::vector<std::uint32_t> rejected;
  {
    const std::lock_guard lock{rejections_->lock};
    rejected.swap(rejections_->actors);
  }
  for (const std::uint32_t actorID : rejected) {
    (void)BeginActor(actorID);
    for (const ApplicationToken &token : PendingFor(actorID)) {
      Report(token, actorID, ApplicationPhase::kFailed,
             "actor refresh could not be queued");
    }
  }
}

ApplicationRecord *ApplicationService::Find(const ApplicationToken &a_token) {
  if (a_token.actorID != 0) {
    const auto found = actors_.find(a_token.actorID);
    return found != actors_.end() ? &found->second : nullptr;
  }
  const auto found = records_.find(a_token.recipeID);
  return found != records_.end() ? &found->second : nullptr;
}

std::string_view ApplicationPhaseName(ApplicationPhase a_phase) noexcept {
  const auto index = static_cast<std::size_t>(a_phase);
  return index < kPhaseNames.size() ? kPhaseNames[index] : "unknown";
}

ApplicationToken
ApplicationService::Begin(std::string a_recipeID,
                          std::vector<std::uint32_t> a_actors) {
  DrainRejections();
  for (auto &[recipeID, record] : records_) {
    if (a_recipeID.empty() || recipeID.empty() || recipeID == a_recipeID) {
      AppendActors(record, a_actors);
      CancelRecord(record);
    }
  }
  if (a_recipeID.empty()) {
    for (const auto &[actorID, record] : actors_) {
      AppendActors(record, a_actors);
    }
  }
  std::erase(a_actors, 0u);
  std::ranges::sort(a_actors);
  a_actors.erase(std::unique(a_actors.begin(), a_actors.end()), a_actors.end());
  for (const std::uint32_t actorID : a_actors) {
    if (const auto actor = actors_.find(actorID); actor != actors_.end()) {
      InvalidateActorScopes(actorID, nextRevision_);
      CancelRecord(actor->second);
    }
  }
  ApplicationRecord record;
  record.token = ApplicationToken{std::move(a_recipeID), nextRevision_++};
  for (const std::uint32_t actorID : a_actors) {
    record.actors.push_back(
        ApplicationActor{actorID, ApplicationPhase::kQueued, {}});
  }
  Aggregate(record);
  const ApplicationToken token = record.token;
  records_.insert_or_assign(token.recipeID, std::move(record));
  PruneActors();
  return token;
}

std::vector<std::uint32_t>
ApplicationService::ActorsFor(std::string_view a_recipeID) const {
  std::vector<std::uint32_t> actors;
  if (const auto record = records_.find(a_recipeID); record != records_.end()) {
    AppendActors(record->second, actors);
  }
  return actors;
}

std::vector<ApplicationToken>
ApplicationService::PendingFor(std::uint32_t a_actorID) const {
  std::vector<ApplicationToken> tokens;
  for (const auto &[recipeID, record] : records_) {
    const auto actor =
        std::ranges::find(record.actors, a_actorID, &ApplicationActor::actorID);
    if (actor != record.actors.end() && Pending(actor->phase)) {
      ApplicationToken token = record.token;
      token.attempt = actor->attempt;
      tokens.push_back(std::move(token));
    }
  }
  if (const auto actor = actors_.find(a_actorID);
      actor != actors_.end() && Pending(actor->second.phase)) {
    tokens.push_back(actor->second.token);
  }
  return tokens;
}

void ApplicationService::Report(const ApplicationToken &a_token,
                                std::uint32_t a_actorID,
                                ApplicationPhase a_phase,
                                std::string a_problem) {
  ApplicationRecord *record = Find(a_token);
  if (!record || record->token.recipeID != a_token.recipeID ||
      record->token.revision != a_token.revision ||
      record->token.actorID != a_token.actorID ||
      a_phase == ApplicationPhase::kQueued ||
      static_cast<std::size_t>(a_phase) >= kPhaseNames.size()) {
    return;
  }
  auto &application = *record;
  const auto actor = std::ranges::find(application.actors, a_actorID,
                                       &ApplicationActor::actorID);
  if (actor == application.actors.end() || !Pending(actor->phase) ||
      actor->attempt != a_token.attempt) {
    return;
  }
  actor->phase = a_phase;
  actor->problem = std::move(a_problem);
  Aggregate(application);
  if (a_token.actorID != 0 && !Pending(a_phase)) {
    PruneActors();
  }
}

void ApplicationService::PruneActors() {
  if (actors_.size() <= kMaxTerminalApplicationActors) {
    return;
  }
  std::vector<std::pair<std::uint64_t, std::uint32_t>> terminal;
  for (const auto &[actorID, record] : actors_) {
    if (!Pending(record.phase)) {
      terminal.emplace_back(record.token.revision, actorID);
    }
  }
  if (terminal.size() <= kMaxTerminalApplicationActors) {
    return;
  }
  std::ranges::sort(terminal);
  const std::size_t count = terminal.size() - kMaxTerminalApplicationActors;
  for (std::size_t i = 0; i < count; ++i) {
    actors_.erase(terminal[i].second);
  }
}

void ApplicationService::Cancel() {
  for (auto &[recipeID, record] : records_) {
    CancelRecord(record);
  }
  for (auto &[actorID, record] : actors_) {
    CancelRecord(record);
  }
  {
    const std::lock_guard lock{rejections_->lock};
    rejections_->actors.clear();
  }
  PruneActors();
}

std::vector<ApplicationRecord> ApplicationService::Snapshot() const {
  std::vector<ApplicationRecord> records;
  records.reserve(records_.size() + actors_.size());
  for (const auto &[recipeID, record] : records_) {
    records.push_back(record);
  }
  for (const auto &[actorID, record] : actors_) {
    records.push_back(record);
  }
  return records;
}
}
