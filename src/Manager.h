#pragma once

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
	struct BoundOutput
	{
		std::size_t                    index = 0;
		std::unique_ptr<RenderedStack> stack;
		std::string                    problem;
	};

	struct BoundGeometry
	{
		RE::NiPointer<RE::BSGeometry>    geometry;
		std::string                      name;
		GeometryInputs                   inputs;
		std::unique_ptr<MaterialBinding> material;
		std::unique_ptr<ShellBinding>    shell;
		std::vector<BoundOutput>         outputs;
	};

	struct Replaced
	{
		std::map<Slot, std::string> slots;
		std::string                 light;
	};

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
		std::vector<AppliedRecipe> recipes;
	};

	class Manager
	{
	public:
		[[nodiscard]] static Manager* GetSingleton();

		void QueueRefresh(RE::FormID a_actorID);
		void QueueRefresh(RE::Actor* a_actor);
		void QueueRetire(RE::FormID a_actorID);
		void QueueEquipFinalize(RE::FormID a_actorID);
		void QueueLoadedActorRefreshes();

		void Clear();
		void ReapplyAll();
		void Isolate(std::string a_recipe, int a_output, int a_layer);
		void RetireAll();

		void EditRecipe(std::string a_id, std::function<void(Recipe&)> a_edit);
		void UndoRecipe(std::string a_id);
		void RedoRecipe(std::string a_id);
		void SaveRecipe(std::string a_id);
		void RevertRecipe(std::string a_id);
		void ReloadRecipes();
		void NewRecipe(std::string a_id, RecipeKey a_key, std::string a_geometry);
		void RenameRecipe(std::string a_from, std::string a_to);
		void BeginPaint(std::string a_active, RecipeKey a_key, Surface a_surface);
		void SetPaintSurface(Surface a_surface);
		void KeepPaint(std::string a_active, std::string a_name);
		void EndPaint();
		void FireAt(RE::FormID a_actorID, std::string a_event, std::string a_node, Vec3 a_offset, float a_random, float a_value);
		void RequestMesh(RE::FormID a_actorID, std::string a_geometry);

		void OnFrame();

		void Fire(RE::FormID a_actorID, const EventRecord& a_event);
		void QueueEvent(RE::FormID a_actorID, EventRecord a_event);

		void               SetEmissivePathEnabled(bool a_enabled);
		[[nodiscard]] bool EmissivePathEnabled() const noexcept { return emissivePathEnabled_; }

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
		std::unordered_set<RE::FormID>                equipped_;
		std::atomic<std::uint64_t>                    generation_{ 0 };

		std::unordered_map<RE::FormID, ActorState> applied_;
		std::unordered_map<std::string, Studio::EditHistory> histories_;
		std::unordered_set<RE::FormID>             loggedNonPBRArmor_;
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
		bool                                       frozenLastTick_ = false;
		Studio::View                               view_{};
		struct IsolateState
		{
			std::string recipe;
			int         output = -1;
			int         layer = -1;
			bool        bySolo = false;
		};
		IsolateState paintReturn_{};

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
