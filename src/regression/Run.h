// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace BetterEnchantmentEffects::Regression {
enum class Outcome : std::uint8_t { kPass, kFail, kBlocked, kAborted };

struct NoRequest {
  [[nodiscard]] bool operator==(const NoRequest &) const = default;
};
struct RequestPending {
  [[nodiscard]] bool operator==(const RequestPending &) const = default;
};
using RequestState = std::variant<NoRequest, RequestPending, Outcome>;

enum class Role : std::uint8_t { kPlayer, kWearer, kControl };
inline constexpr std::size_t kRoleCount = 3;

enum class Item : std::uint8_t { kFixture, kPlainCuirass };
inline constexpr std::size_t kItemCount = 2;

enum class View : std::uint8_t { kFirstPerson, kThirdPerson };

enum class QueuedWork : std::uint8_t { kNothing, kApply, kEdit };
enum class Session : std::uint8_t { kGesture, kPaint };
enum class TrackedWork : std::uint8_t { kEdit, kGesture };

enum class WorkOutcome : std::uint8_t {
  kNone,
  kPending,
  kApplied,
  kCancelledByLoad,
  kFailed
};

struct Settle {
  std::uint32_t frames = 0;
};
struct Solo {
  std::string_view recipe;
};
struct RestoreView {};
struct Spawn {
  Role role = Role::kWearer;
};
struct Despawn {
  Role role = Role::kWearer;
};
struct Disable {
  Role role = Role::kWearer;
};
struct Enable {
  Role role = Role::kWearer;
};
struct Equip {
  Role role = Role::kPlayer;
  Item item = Item::kFixture;
};
struct Unequip {
  Role role = Role::kPlayer;
  Item item = Item::kFixture;
};
struct Remove {
  Role role = Role::kPlayer;
  Item item = Item::kFixture;
};
struct Apply {
  Role role = Role::kPlayer;
};
struct Retire {
  Role role = Role::kPlayer;
};
struct AwaitRendered {
  Role role = Role::kPlayer;
};
struct AwaitRetired {
  Role role = Role::kPlayer;
};
struct AwaitBaseline {
  Role role = Role::kPlayer;
};
struct ExpectEffect {
  Role role = Role::kPlayer;
};
struct HoldUntouched {
  Role role = Role::kControl;
  std::uint32_t frames = 0;
};
struct LeaveCell {};
struct ReturnToStart {};
struct SetView {
  View view = View::kThirdPerson;
};
struct LoadDuring {
  QueuedWork work = QueuedWork::kNothing;
  std::string_view recipe;
};
struct ExpectAborted {};
struct ExpectCancelled {
  TrackedWork work = TrackedWork::kEdit;
};
struct ExpectSettled {
  TrackedWork work = TrackedWork::kEdit;
};
struct DeleteScratch {};
struct DuplicateToScratch {
  std::string_view from;
};
struct SetScratchOpacity {
  float value = 1.0f;
};
struct SaveScratch {};
struct ExpectScratch {
  float opacity = 1.0f;
};
struct SpawnCrowd {
  std::uint32_t count = 0;
};
struct AwaitCrowdRendered {};
struct DespawnCrowd {};
struct HoldFor {
  std::string_view window;
  std::uint32_t seconds = 0;
};
struct BeginSession {
  Session session = Session::kPaint;
  std::string_view recipe;
};
struct AwaitSessionActive {
  Session session = Session::kPaint;
};
struct BeginWindow {
  std::string_view window;
};
struct EndWindow {
  std::string_view window;
};
struct AwaitIdle {};
using Step = std::variant<
    Settle, Solo, RestoreView, Spawn, Despawn, Disable, Enable, Equip, Unequip,
    Remove, Apply, Retire, AwaitRendered, AwaitRetired, AwaitBaseline,
    ExpectEffect, HoldUntouched, LeaveCell, ReturnToStart, SetView, LoadDuring,
    ExpectAborted, ExpectCancelled, ExpectSettled, DeleteScratch,
    DuplicateToScratch, SetScratchOpacity, SaveScratch, ExpectScratch,
    SpawnCrowd, AwaitCrowdRendered, DespawnCrowd, HoldFor, BeginWindow,
    EndWindow, BeginSession, AwaitSessionActive, AwaitIdle>;

struct Case {
  std::string_view name;
  std::span<const Step> body;
  std::span<const Step> cleanup;
};
using CaseList = std::vector<std::reference_wrapper<const Case>>;

struct RunRequest {
  std::string run;
  std::string save;
  CaseList suite;
};

struct ArmorView {
  bool equipped = false;
  bool carried = false;
};

struct ActorView {
  bool present = false;
  bool bodyArmorWorn = false;
  std::array<ArmorView, kItemCount> armor{};
  bool live = false;
  std::uint64_t renderedAttempt = 0;
  std::uint32_t traces = 0;
  std::string application;
};

struct Activity {
  std::uint32_t pendingApplications = 0;
  bool paintActive = false;
  bool gestureActive = false;
  bool fileOperationPending = false;
  WorkOutcome editOutcome = WorkOutcome::kNone;
  WorkOutcome gestureOutcome = WorkOutcome::kNone;
  WorkOutcome fileOutcome = WorkOutcome::kNone;
  std::string detail;
};

struct CrowdView {
  std::uint32_t present = 0;
  std::uint32_t rendered = 0;
};

struct RecipeView {
  bool loaded = false;
  bool dirty = false;
  std::optional<float> firstOpacity;
};

struct Observation {
  std::array<ActorView, kRoleCount> actors{};
  Activity activity;
  RecipeView scratch;
  CrowdView crowd;
  std::uint64_t nowMs = 0;
  std::uint32_t loads = 0;
  std::array<bool, kItemCount> itemsLoaded{};
  bool npcEffects = false;
  bool firstPerson = false;
  bool awayFromStart = false;
  RequestState request = NoRequest{};
};

struct SoloRecipe {
  std::string_view recipe;
};
struct RestoreSolo {};
struct SpawnActor {
  Role role = Role::kWearer;
};
struct DespawnActor {
  Role role = Role::kWearer;
};
struct DisableActor {
  Role role = Role::kWearer;
};
struct EnableActor {
  Role role = Role::kWearer;
};
struct AddAndEquip {
  Role role = Role::kPlayer;
  Item item = Item::kFixture;
};
struct EquipCarried {
  Role role = Role::kPlayer;
  Item item = Item::kFixture;
};
struct UnequipArmor {
  Role role = Role::kPlayer;
  Item item = Item::kFixture;
};
struct RemoveArmor {
  Role role = Role::kPlayer;
  Item item = Item::kFixture;
};
struct SubmitApply {
  Role role = Role::kPlayer;
};
struct SubmitRetire {
  Role role = Role::kPlayer;
};
struct AbortRequest {};
struct TravelAway {};
struct TravelBack {};
struct SetCamera {
  View view = View::kThirdPerson;
};
struct CopyRecipe {
  std::string_view from;
  std::string_view to;
};
struct EditOpacity {
  std::string_view recipe;
  float value = 1.0f;
};
struct WriteRecipe {
  std::string_view recipe;
};
struct RemoveRecipe {
  std::string_view recipe;
};
struct PlaceCrowd {
  std::uint32_t count = 0;
};
struct RemoveCrowd {};
struct OpenSession {
  Session session = Session::kPaint;
  std::string_view recipe;
};
struct ReloadDuring {
  QueuedWork work = QueuedWork::kNothing;
  std::string_view recipe;
};
struct Quit {};
using Command =
    std::variant<SoloRecipe, RestoreSolo, SpawnActor, DespawnActor,
                 DisableActor, EnableActor, AddAndEquip, EquipCarried,
                 UnequipArmor, RemoveArmor, SubmitApply, SubmitRetire,
                 AbortRequest, TravelAway, TravelBack, SetCamera, CopyRecipe,
                 EditOpacity, WriteRecipe, RemoveRecipe, PlaceCrowd,
                 RemoveCrowd, OpenSession, ReloadDuring, Quit>;

struct StepResult {
  std::string_view caseName;
  std::size_t step = 0;
  std::string action;
  Outcome outcome = Outcome::kPass;
  std::uint32_t frames = 0;
  std::string reason;
};
struct CaseResult {
  std::string_view caseName;
  Outcome outcome = Outcome::kPass;
};
struct RunEnd {
  Outcome outcome = Outcome::kPass;
  std::string reason;
};
struct WindowBegins {
  std::string_view window;
};
struct WindowEnds {
  std::string_view window;
};
using ResultLine =
    std::variant<StepResult, CaseResult, RunEnd, WindowBegins, WindowEnds>;

struct Ownership {
  std::array<std::array<bool, kItemCount>, kRoleCount> added{};
  std::array<bool, kRoleCount> spawned{};
  bool away = false;
  std::uint32_t loadTarget = 0;
  std::uint32_t crowd = 0;
  std::uint64_t holdUntilMs = 0;
};

enum class Section : std::uint8_t { kBody, kCleanup };

struct Settling {
  std::uint32_t frames = 0;
};
struct Running {
  std::size_t caseIndex = 0;
  Section section = Section::kBody;
  std::size_t step = 0;
  std::uint32_t frames = 0;
  bool started = false;
  Outcome caseOutcome = Outcome::kPass;
};
struct Ending {
  std::uint32_t frames = 0;
};
struct Finished {};
using Phase = std::variant<Settling, Running, Ending, Finished>;

struct RunState {
  CaseList cases;
  Phase phase = Settling{};
  Outcome runOutcome = Outcome::kPass;
  Ownership owned;
  std::array<std::uint64_t, kRoleCount> renderMarks{};
};

struct Advanced {
  RunState state;
  std::optional<Command> command;
  std::vector<ResultLine> lines;
};

inline constexpr std::uint32_t kSettleFrames = 120;
inline constexpr std::string_view kScratchRecipe = "regression-scratch";
inline constexpr std::uint32_t kMaxCrowd = 64;
inline constexpr std::uint32_t kEndingFrames = 10;
inline constexpr std::size_t kMaxSuiteCases = 64;

[[nodiscard]] std::string_view OutcomeName(Outcome a_outcome);
[[nodiscard]] std::size_t RoleIndex(Role a_role);
[[nodiscard]] std::string StepLabel(const Step &a_step);
[[nodiscard]] std::span<const Case> Catalog();
[[nodiscard]] const Case *FindCase(std::string_view a_name);
[[nodiscard]] std::expected<RunRequest, std::string>
ParseRunRequest(std::string_view a_text, std::int64_t a_nowSeconds);
[[nodiscard]] RunState BeginRun(const RunRequest &a_request);
[[nodiscard]] Advanced Advance(RunState a_state, const Observation &a_seen);
[[nodiscard]] bool Done(const RunState &a_state);
[[nodiscard]] std::string StartLineJson(std::string_view a_run,
                                        std::string_view a_build,
                                        std::string_view a_source,
                                        std::string_view a_trace);
[[nodiscard]] std::string ResultLineJson(const ResultLine &a_line);
}
