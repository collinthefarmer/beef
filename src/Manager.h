#pragma once

// Actors, their worn pieces, the recipes that resolve for each, and the
// per-tick drive of signals, stacks and bindings. Thin: resolution lives in
// Recipe, evaluation in Signals, rendering in Compositor, engine writes in
// Binding. The menu reads a copied snapshot, never the live state.

#include "Binding.h"
#include "Compositor.h"
#include "Environment.h"
#include "PCH.h"
#include "Recipe.h"
#include "Signals.h"
#include "History.h"
#include "Snapshot.h"
#include "View.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace WornEnchantmentPBR
{
	// One output bound on one geometry.
	struct BoundOutput
	{
		std::size_t                    index = 0;  // into Recipe::outputs
		std::unique_ptr<RenderedStack> stack;      // material outputs
		std::string                    problem;    // why it is not bound, when it is not
	};

	struct BoundGeometry
	{
		RE::NiPointer<RE::BSGeometry>    geometry;
		std::string                      name;
		GeometryInputs                   inputs;  // the material's maps and the masks rendered for it
		std::unique_ptr<MaterialBinding> material;
		std::unique_ptr<ShellBinding>    shell;
		std::vector<BoundOutput>         outputs;
	};

	// The outputs a higher-priority recipe's `replace` takes away from a
	// lower one: slots by the recipe that took them, and the light.
	struct Replaced
	{
		std::map<Slot, std::string> slots;
		std::string                 light;
	};

	// One recipe applied to one worn piece.
	struct AppliedRecipe
	{
		const Recipe*                       recipe = nullptr;
		RecipeKey                           key;
		int                                 priority = 0;
		std::shared_ptr<const SignalGraph>  graph;
		std::unique_ptr<SignalState>        signals;
		std::unique_ptr<ActorEnvironment>   environment;
		std::vector<BoundGeometry>          geometries;
		std::unique_ptr<LightBinding>       light;
		std::optional<std::size_t>          lightOutput;
		Replaced                            replaced;
		std::uint32_t                       startMS = 0;
		float                               lastTime = 0.0f;
	};

	struct AppliedPiece
	{
		RE::FormID                 armor = 0;
		std::string                armorName;
		bool                       firstPerson = false;
		WornPiece                  piece;
		std::vector<AppliedRecipe> recipes;  // merge order
	};

	class Manager
	{
	public:
		[[nodiscard]] static Manager* GetSingleton();

		// Called from event sinks (any thread). Work is posted to the game thread.
		void QueueRefresh(RE::FormID a_actorID);
		void QueueRefresh(RE::Actor* a_actor);
		void QueueRetire(RE::FormID a_actorID);
		void QueueEquipFinalize(RE::FormID a_actorID);
		void QueueLoadedActorRefreshes();

		// kPreLoadGame: restore everything and invalidate queued tasks.
		void Clear();
		void ReapplyAll();
		// Isolate one recipe (empty: none), and within it one output and one
		// layer (-1: all). Resolution applies an isolated recipe alone, so
		// changing which recipe is isolated re-applies every actor; the
		// output and layer are tick-time filters and cost nothing. Posted,
		// like RetireAll and ReapplyAll: any thread may call them.
		void Isolate(std::string a_recipe, int a_output, int a_layer);
		void RetireAll();

		// Recipe editing, from any thread; each runs on the game thread with
		// every actor wearing the recipe retired first and re-applied after,
		// so no binding holds a pointer into a recipe while it changes.
		void EditRecipe(std::string a_id, std::function<void(Recipe&)> a_edit);
		// Back one edit, or forward one, through the same retire-and-reapply
		// path; the history is per recipe and Revert clears it.
		void UndoRecipe(std::string a_id);
		void RedoRecipe(std::string a_id);
		void SaveRecipe(std::string a_id);
		void RevertRecipe(std::string a_id);
		// Retires everything, re-reads every recipe file, re-applies.
		void ReloadRecipes();
		// An empty recipe keyed to the armor, from any thread; everything is
		// retired around it since the store's list moves, then re-applied,
		// so the recipe resolves onto the armor's wearers at once.
		void NewRecipe(std::string a_id, RecipeKey a_key, std::string a_geometry);
		void RenameRecipe(std::string a_from, std::string a_to);  // the history, the isolate and Paint's return follow the id
		// Painting: a transient paint recipe (Studio::PaintRecipe) cloned
		// from the active recipe, keyed as given, isolated so the region
		// shows alone; the surface its emissive previews on can change;
		// Keep copies the scratch into the active recipe under a name as one
		// history step and ends the session; End drops it. Everything is
		// retired around the store's list moving, then re-applied.
		void BeginPaint(std::string a_active, RecipeKey a_key, Surface a_surface);
		void SetPaintSurface(Surface a_surface);
		void KeepPaint(std::string a_active, std::string a_name);
		void EndPaint();
		// A firing placed on the actor: at the named node (its world position
		// plus the offset, scattered within a_random units), carrying the
		// value; with no node the firing has no place.
		void FireAt(RE::FormID a_actorID, std::string a_event, std::string a_node, Vec3 a_offset, float a_random, float a_value);
		// Reads the mesh of a bound geometry on the game thread, so the
		// snapshot can list its partitions and bones.
		void RequestMesh(RE::FormID a_actorID, std::string a_geometry);

		// Once per frame from the PlayerCharacter::Update hook (game thread).
		void OnFrame();

		// An event on the bus for one actor's applied recipes (game thread).
		void Fire(RE::FormID a_actorID, const EventRecord& a_event);
		// The same from any thread; delivered on the game thread.
		void QueueEvent(RE::FormID a_actorID, EventRecord a_event);

		void               SetEmissivePathEnabled(bool a_enabled);
		[[nodiscard]] bool EmissivePathEnabled() const noexcept { return emissivePathEnabled_; }

		// How the piece is looked at (freeze, scrub, speed, isolate, mute) is
		// changed on the game thread through a posted change; the menu reads
		// the copy inside the latest snapshot, never the live record.
		void UpdateView(std::function<void(Studio::View&)> a_change);

		struct Status
		{
			bool          emissivePath = false;
			bool          layoutVerified = false;
			bool          runtimeLab = false;
			std::uint32_t actors = 0;
			std::uint32_t pieces = 0;
			std::uint32_t recipes = 0;
			std::uint32_t geometries = 0;
			std::uint32_t shells = 0;
			std::uint32_t lights = 0;
			std::uint32_t tickMS = 0;
		};
		[[nodiscard]] Status GetStatus() const;

		// The menu's read side. The tick builds a snapshot (Snapshot.h) once
		// per tick while the menu watches, full rows for the watched piece
		// and light rows for the rest, and publishes it whole; no engine
		// pointers in it except the texture views the thumbnails draw. Watch
		// is called every frame a page draws; a second without it stops the
		// building. Any thread.
		using Snapshot = Studio::Snapshot;
		void                                          Watch(const std::optional<Studio::SnapshotRequest>& a_request);
		[[nodiscard]] std::shared_ptr<const Snapshot> LatestSnapshot() const;

	private:
		struct ActorState
		{
			std::vector<AppliedPiece> pieces;
		};

		void PostTask(std::function<void()> a_task);
		void RunRefresh(RE::FormID a_actorID, std::uint64_t a_generation);
		void Refresh(RE::Actor* a_actor);
		void Retire(RE::FormID a_actorID);
		// Game thread: retires the wearers of a recipe, runs the action, re-applies them.
		void WithRecipeRetired(std::string_view a_id, const std::function<void()>& a_action);
		void RetireEveryActor();
		void FireDueFinalizes();
		void Tick(std::uint32_t a_nowMS);
		void TickRecipe(AppliedRecipe& a_applied, float a_time, float a_delta);

		[[nodiscard]] std::vector<AppliedPiece> CollectPieces(RE::Actor* a_actor, bool a_firstPerson);
		void                                    ApplyRecipes(RE::Actor* a_actor, AppliedPiece& a_piece, RE::NiAVObject* a_clone);
		bool                                    ApplyGeometry(RE::Actor* a_actor, RE::NiAVObject* a_root, AppliedRecipe& a_applied, RE::BSGeometry* a_geometry, RE::BSLightingShaderProperty* a_property);
		bool                                    LayoutSanityCheck(RE::BSLightingShaderProperty* a_property);

		std::mutex                                    queueLock_;
		std::unordered_set<RE::FormID>                pending_;
		std::unordered_set<RE::FormID>                rerun_;
		std::unordered_map<RE::FormID, std::uint32_t> finalizeDue_;
		std::unordered_set<RE::FormID>                equipped_;  // actors whose next apply fires `equip`
		std::atomic<std::uint64_t>                    generation_{ 0 };

		std::unordered_map<RE::FormID, ActorState> applied_;
		std::unordered_map<std::string, Studio::EditHistory> histories_;  // by recipe id; game thread
		std::unordered_set<RE::FormID>             loggedNonPBRArmor_;
		// A recipe's clock survives a retire that is followed by a re-apply
		// within a moment (an edit, isolate, re-apply all), so a change never
		// snaps the animation back to zero. Keyed by actor and recipe id;
		// entries older than kCarryWindowMS are ignored, so a piece put back
		// on later starts fresh.
		struct CarriedTime
		{
			float         seconds = 0.0f;
			std::uint32_t retiredMS = 0;
		};
		std::map<std::pair<RE::FormID, std::string>, CarriedTime> carriedTimes_;
		static constexpr std::uint32_t                            kCarryWindowMS = 2000;
		std::uint32_t                              lastTickMS_ = 0;
		bool                                       emissivePathEnabled_ = true;
		bool                                       layoutVerified_ = false;
		bool                                       frozenLastTick_ = false;  // to notice the tick that leaves freeze
		Studio::View                               view_{};  // game thread
		// The isolate the view had when Paint began, put back when it ends.
		struct IsolateState
		{
			std::string recipe;
			int         output = -1;
			int         layer = -1;
			bool        bySolo = false;
		};
		IsolateState paintReturn_{};

		// The published snapshot and what the menu watches; the lock covers
		// the pointer swap and the request, never a build.
		[[nodiscard]] Snapshot BuildSnapshot(const std::optional<Studio::SnapshotRequest>& a_request) const;
		void                   PublishSnapshot(std::uint32_t a_nowMS);
		mutable std::mutex                     snapshotLock_;
		std::shared_ptr<const Snapshot>        latest_ = std::make_shared<Snapshot>();
		std::optional<Studio::SnapshotRequest> watch_;
		std::uint32_t                          watchedMS_ = 0;
		std::uint64_t                          snapshotVersion_ = 0;
		static constexpr std::uint32_t         kWatchWindowMS = 1000;
	};
}
