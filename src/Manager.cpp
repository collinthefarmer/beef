#include "Manager.h"

#include "Edits.h"
#include "EngineForms.h"
#include "Studio.h"
#include "Events.h"
#include "RecipeStore.h"
#include "Settings.h"

namespace WornEnchantmentPBR
{
	namespace
	{
		// The engine swaps the worn mesh after TESEquipEvent fires; the original
		// plugin re-attaches 100 ms later (plugin.c 1461-1482).
		constexpr std::uint32_t kEquipFinalizeDelayMS = 100;

		std::uint32_t NowMS()
		{
			return RE::GetDurationOfApplicationRunTime();
		}

		RE::BSLightingShaderProperty* LightingPropertyOf(RE::BSGeometry* a_geometry)
		{
			auto* property = a_geometry->GetGeometryRuntimeData().properties[RE::BSGeometry::States::kEffect].get();
			return property ? netimmerse_cast<RE::BSLightingShaderProperty*>(property) : nullptr;
		}

		// A player-applied enchantment on the worn extra list beats the base
		// record (plugin.c 2013-2039).
		RE::MagicItem* WornEnchantment(RE::Actor* a_actor, RE::TESObjectARMO* a_armor)
		{
			auto inventory = a_actor->GetInventory([&](RE::TESBoundObject& a_object) { return &a_object == a_armor; });
			for (auto& [object, pair] : inventory) {
				auto& [count, entry] = pair;
				if (count <= 0 || !entry || !entry->extraLists) {
					continue;
				}
				for (auto* xList : *entry->extraLists) {
					if (!xList || !(xList->HasType(RE::ExtraDataType::kWorn) || xList->HasType(RE::ExtraDataType::kWornLeft))) {
						continue;
					}
					if (const auto* xEnch = xList->GetByType<RE::ExtraEnchantment>(); xEnch && xEnch->enchantment) {
						return xEnch->enchantment;
					}
				}
			}
			return a_armor->formEnchanting;
		}

		std::string TexturePath(const RE::NiPointer<RE::NiSourceTexture>& a_texture)
		{
			return a_texture && a_texture->name.c_str() ? a_texture->name.c_str() : "";
		}

		// The graph's first diagnostic on a signal row: why the row is inert.
		std::string InertReason(const SignalGraph& a_graph, std::string_view a_name)
		{
			const auto where = std::format("signal {}", a_name);
			for (const auto& d : a_graph.Diagnostics()) {
				if (d.where == where) {
					return d.message;
				}
			}
			return {};
		}

		// The slots a binding wrote, each with the binding's refusal of that
		// slot when it has one, which is how material rules reach the board.
		std::vector<Studio::SlotRow> SlotRows(const SlotTarget& a_target)
		{
			std::vector<Studio::SlotRow> rows;
			for (const auto& s : a_target.Slots()) {
				rows.push_back({ s.slot, s.original, s.written, a_target.Problem(s.slot) });
			}
			return rows;
		}

		const char* KindName(const Signal& a_signal)
		{
			return Match(
				a_signal.kind,
				[](const ConstantSignal&) { return "constant"; }, [](const PulseSignal&) { return "pulse"; }, [](const RampSignal&) { return "ramp"; },
				[](const EfshSignal&) { return "efsh"; }, [](const ActorValueSignal&) { return "av"; }, [](const ActorStateSignal&) { return "actorState"; },
				[](const EnchantmentSignal&) { return "enchantment"; }, [](const TriggerSignal&) { return "trigger"; }, [](const PayloadSignal&) { return "payload"; },
				[](const CounterSignal&) { return "counter"; }, [](const AccumulateSignal&) { return "accumulate"; }, [](const NoiseSignal&) { return "noise"; },
				[](const GradientSignal&) { return "gradient"; }, [](const DeltaSignal&) { return "delta"; }, [](const SmoothSignal&) { return "smooth"; },
				[](const ExprSignal&) { return "expr"; });
		}
	}

	Manager* Manager::GetSingleton()
	{
		static Manager singleton;
		return &singleton;
	}

	// ------------------------------------------------------------- queueing
	// One pending refresh per actor; a duplicate request while pending makes
	// the refresh run once more afterwards (plugin.c 1513-1608).

	namespace
	{
		// The surface an output writes on this geometry; null when it is not bound.
		SlotTarget* TargetFor(BoundGeometry& a_bound, Surface a_surface)
		{
			if (a_surface == Surface::kShell) {
				return a_bound.shell.get();
			}
			return a_bound.material.get();
		}

		float ScalarOr(const SignalState& a_signals, const std::optional<Param>& a_param, float a_default)
		{
			return a_param ? a_signals.Resolve(*a_param) : a_default;
		}

		Vec3 ColorOr(const SignalState& a_signals, const std::optional<Vec3Param>& a_param, const Vec3& a_default)
		{
			return a_param ? a_signals.Resolve(*a_param) : a_default;
		}

		// One output's writes for this tick: the composite into the slot, then
		// the slot's scalars. A hidden output puts the original map back and
		// zeroes what would show.
		void WriteSlot(SlotTarget& a_target, const MaterialOutput& a_output, const SignalState& a_signals, RE::NiSourceTexture* a_texture, bool a_shown)
		{
			const Vec3 white{ 1.0f, 1.0f, 1.0f };
			a_target.WriteTexture(a_output.slot, a_shown ? a_texture : nullptr);
			switch (a_output.slot) {
			case Slot::kEmissive:
				a_target.WriteEmissive(white, a_shown ? ScalarOr(a_signals, a_output.scalars.strength, 1.0f) : 0.0f);
				break;
			case Slot::kFuzz:
				a_target.WriteFuzz(ColorOr(a_signals, a_output.scalars.color, white), a_shown ? ScalarOr(a_signals, a_output.scalars.weight, 1.0f) : 0.0f);
				break;
			case Slot::kHeight:
				a_target.WriteHeightScale(a_shown ? ScalarOr(a_signals, a_output.scalars.scale, 1.0f) : 0.0f);
				break;
			case Slot::kGlint:
				a_target.WriteGlint(ScalarOr(a_signals, a_output.scalars.screenSpaceScale, 1.5f), ScalarOr(a_signals, a_output.scalars.logMicrofacetDensity, 40.0f),
					ScalarOr(a_signals, a_output.scalars.microfacetRoughness, 0.015f), ScalarOr(a_signals, a_output.scalars.densityRandomization, 2.0f), a_shown);
				break;
			case Slot::kCoat:
				a_target.WriteCoat(ScalarOr(a_signals, a_output.scalars.roughness, 1.0f), a_shown ? ScalarOr(a_signals, a_output.scalars.level, 0.04f) : 0.0f);
				break;
			case Slot::kSubsurface:
				a_target.WriteSubsurface(ColorOr(a_signals, a_output.scalars.color, white), a_shown ? ScalarOr(a_signals, a_output.scalars.thickness, 1.0f) : 0.0f);
				break;
			default:
				break;
			}
		}

		// What the recipes merging after this one take over with `replace`:
		// a slot's stacks and scalars on either surface, or the light.
		Replaced ReplacedBy(std::span<const AppliedRecipe> a_later)
		{
			Replaced replaced;
			for (const auto& later : a_later) {
				for (const auto& output : later.recipe->outputs) {
					if (const auto* material = Get<MaterialOutput>(output); material && material->replace) {
						replaced.slots.emplace(material->slot, later.recipe->id);
					} else if (const auto* light = Get<LightOutput>(output); light && light->replace) {
						replaced.light = later.recipe->id;
					}
				}
			}
			return replaced;
		}

		// The layers of one output the view hides by solo or mute, as the
		// compositor's filter. Called only when the view hides some layer of
		// some recipe, so the common tick never walks a stack.
		LayerFilter HiddenLayers(const Studio::View& a_view, const std::string& a_recipe, std::size_t a_output, std::size_t a_layerCount)
		{
			LayerFilter filter;
			if (!a_view.FiltersLayers(a_recipe, a_output)) {
				return filter;
			}
			for (std::size_t i = 0; i < a_layerCount; ++i) {
				if (!a_view.LayerShown(a_recipe, a_output, i)) {
					filter.hidden.push_back(i);
				}
			}
			return filter;
		}

		// `equip` carries the centre of the first bound geometry as its position.
		EventRecord EquipEvent(const std::vector<AppliedPiece>& a_pieces)
		{
			EventRecord record;
			record.id = "equip";
			for (const auto& piece : a_pieces) {
				for (const auto& applied : piece.recipes) {
					for (const auto& bound : applied.geometries) {
						if (bound.geometry) {
							const auto& c = bound.geometry->worldBound.center;
							record.payload.position = Vec3{ c.x, c.y, c.z };
							return record;
						}
					}
				}
			}
			return record;
		}
	}

	void Manager::PostTask(std::function<void()> a_task)
	{
		const auto* tasks = SKSE::GetTaskInterface();
		if (!tasks) {
			logger::error("no SKSE task interface; dropping work");
			return;
		}
		tasks->AddTask(std::move(a_task));
	}

	void Manager::QueueRefresh(RE::FormID a_actorID)
	{
		if (a_actorID == 0) {
			return;
		}
		std::uint64_t generation = 0;
		{
			std::scoped_lock lock{ queueLock_ };
			if (!pending_.insert(a_actorID).second) {
				rerun_.insert(a_actorID);
				return;
			}
			generation = generation_.load();
		}
		PostTask([this, a_actorID, generation] { RunRefresh(a_actorID, generation); });
	}

	void Manager::QueueRefresh(RE::Actor* a_actor)
	{
		if (a_actor) {
			QueueRefresh(a_actor->GetFormID());
		}
	}

	void Manager::QueueRetire(RE::FormID a_actorID)
	{
		if (a_actorID == 0) {
			return;
		}
		const auto generation = generation_.load();
		PostTask([this, a_actorID, generation] {
			if (generation == generation_.load()) {
				Retire(a_actorID);
			}
		});
	}

	// SKSE drains its task queue until empty, so a task may never re-post
	// itself to wait for a later frame (NOTES 12); due times fire from OnFrame.
	void Manager::QueueEquipFinalize(RE::FormID a_actorID)
	{
		if (a_actorID == 0) {
			return;
		}
		std::scoped_lock lock{ queueLock_ };
		finalizeDue_[a_actorID] = NowMS() + kEquipFinalizeDelayMS;
	}

	void Manager::QueueEvent(RE::FormID a_actorID, EventRecord a_event)
	{
		if (a_actorID == 0) {
			return;
		}
		PostTask([this, a_actorID, event = std::move(a_event)] { Fire(a_actorID, event); });
	}

	void Manager::FireDueFinalizes()
	{
		std::vector<RE::FormID> due;
		{
			std::scoped_lock lock{ queueLock_ };
			const auto now = NowMS();
			for (auto it = finalizeDue_.begin(); it != finalizeDue_.end();) {
				if (static_cast<std::int32_t>(now - it->second) >= 0) {
					due.push_back(it->first);
					// The refresh that follows the delay sees the new piece's 3D, so
					// it is the apply that fires `equip`; the immediate refresh on
					// the equip event does not (the piece is not there yet).
					equipped_.insert(it->first);
					it = finalizeDue_.erase(it);
				} else {
					++it;
				}
			}
		}
		for (const auto id : due) {
			QueueRefresh(id);
		}
	}

	void Manager::QueueLoadedActorRefreshes()
	{
		if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			QueueRefresh(player);
		}
		if (GetSettings().playerOnly) {
			return;
		}
		if (auto* lists = RE::ProcessLists::GetSingleton()) {
			lists->ForEachHighActor([this](RE::Actor* a_actor) {
				QueueRefresh(a_actor);
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}
	}

	void Manager::Clear()
	{
		{
			std::scoped_lock lock{ queueLock_ };
			pending_.clear();
			rerun_.clear();
			finalizeDue_.clear();
			equipped_.clear();
			++generation_;
		}
		const auto count = applied_.size();
		applied_.clear();  // bindings restore in their destructors
		loggedNonPBRArmor_.clear();
		carriedTimes_.clear();
		TextureLab::GetSingleton()->Clear();
		logger::info("cleared {} actor states", count);
	}

	void Manager::Isolate(std::string a_recipe, int a_output, int a_layer)
	{
		const bool changed = view_.isolateRecipe != a_recipe;
		view_.isolateRecipe = std::move(a_recipe);
		view_.isolateOutput = a_output;
		view_.isolateLayer = a_layer;
		if (changed) {
			ReapplyAll();
		}
	}

	void Manager::ReapplyAll()
	{
		std::vector<RE::FormID> ids;
		for (const auto& [id, state] : applied_) {
			ids.push_back(id);
		}
		for (const auto id : ids) {
			QueueRefresh(id);
		}
		if (ids.empty()) {
			QueueLoadedActorRefreshes();
		}
	}

	void Manager::RetireAll()
	{
		std::vector<RE::FormID> ids;
		for (const auto& [id, state] : applied_) {
			ids.push_back(id);
		}
		for (const auto id : ids) {
			QueueRetire(id);
		}
	}

	void Manager::SetEmissivePathEnabled(bool a_enabled)
	{
		emissivePathEnabled_ = a_enabled;
	}

	void Manager::RunRefresh(RE::FormID a_actorID, std::uint64_t a_generation)
	{
		if (a_generation != generation_.load()) {
			return;
		}
		bool rerun = false;
		{
			std::scoped_lock lock{ queueLock_ };
			pending_.erase(a_actorID);
			rerun = rerun_.erase(a_actorID) > 0;
		}
		auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_actorID);
		if (!actor) {
			Retire(a_actorID);
		} else {
			Refresh(actor);
		}
		if (rerun) {
			QueueRefresh(a_actorID);
		}
	}

	// --------------------------------------------------------------- apply

	void Manager::Refresh(RE::Actor* a_actor)
	{
		const auto& settings = GetSettings();
		const auto  actorID = a_actor->GetFormID();
		Retire(actorID);
		if (!settings.enableShaders || !emissivePathEnabled_ || a_actor->IsDeleted()) {
			return;
		}
		const bool isPlayer = a_actor->IsPlayerRef();
		if (settings.playerOnly && !isPlayer) {
			return;
		}
		if (!a_actor->Is3DLoaded()) {
			if (settings.verboseLogging) {
				logger::info("actor {:08X} ({}): 3D not loaded, skipped", actorID, a_actor->GetName());
			}
			return;
		}
		ActorState state;
		if (settings.thirdPerson) {
			for (auto& piece : CollectPieces(a_actor, false)) {
				state.pieces.push_back(std::move(piece));
			}
		}
		if (settings.firstPerson && isPlayer) {
			for (auto& piece : CollectPieces(a_actor, true)) {
				state.pieces.push_back(std::move(piece));
			}
		}
		std::erase_if(state.pieces, [](const AppliedPiece& p) { return p.recipes.empty(); });
		TextureLab::GetSingleton()->InvalidatePreviews();
		if (state.pieces.empty()) {
			return;
		}
		if (settings.verboseLogging) {
			std::size_t recipes = 0;
			for (const auto& p : state.pieces) {
				recipes += p.recipes.size();
			}
			logger::info("actor {:08X} ({}): {} piece(s), {} recipe(s) applied", actorID, a_actor->GetName(), state.pieces.size(), recipes);
		}
		applied_[actorID] = std::move(state);
		WatchAnimationEvents(a_actor);

		bool equipped = false;
		{
			std::scoped_lock lock{ queueLock_ };
			equipped = equipped_.erase(actorID) > 0;
		}
		if (equipped) {
			Fire(actorID, EquipEvent(applied_[actorID].pieces));
		}
	}

	// Every worn armor on the biped with a PBR geometry, described for resolution.
	std::vector<AppliedPiece> Manager::CollectPieces(RE::Actor* a_actor, bool a_firstPerson)
	{
		const auto&               settings = GetSettings();
		std::vector<AppliedPiece> out;
		const auto&               biped = a_actor->GetBiped(a_firstPerson);
		if (!biped) {
			return out;
		}
		const auto                          loaded = LoadedRecipes();
		const bool                          walkUnenchanted = AnyUnenchantedKey(loaded);
		std::unordered_set<RE::NiAVObject*> seenClones;
		for (const auto& object : biped->objects) {
			auto* armor = object.item ? object.item->As<RE::TESObjectARMO>() : nullptr;
			auto* clone = object.partClone.get();
			if (!armor || !clone || !seenClones.insert(clone).second) {
				continue;
			}
			AppliedPiece piece;
			piece.armor = armor->GetFormID();
			piece.armorName = armor->GetName() ? armor->GetName() : "";
			piece.firstPerson = a_firstPerson;
			piece.piece.armor = FormKeyFor(*armor);
			if (auto* magic = WornEnchantment(a_actor, armor)) {
				piece.piece.enchantment = FormKeyFor(*magic);
				if (const auto* costliest = magic->GetCostliestEffectItem(); costliest && costliest->baseEffect) {
					piece.piece.magicEffect = FormKeyFor(*costliest->baseEffect);
				}
				if (auto* shader = ShaderFor(magic)) {
					piece.piece.effectShader = FormKeyFor(*shader);
				}
			}
			if (!piece.piece.Enchanted() && !walkUnenchanted) {
				continue;
			}
			for (std::uint32_t i = 0; i < armor->GetNumKeywords(); ++i) {
				if (const auto keyword = armor->GetKeywordAt(i); keyword && *keyword) {
					piece.piece.keywords.push_back(FormKeyFor(**keyword));
				}
			}
			RE::BSVisit::TraverseScenegraphGeometries(clone, [&](RE::BSGeometry* a_geometry) {
				const std::string_view name{ a_geometry->name.c_str() ? a_geometry->name.c_str() : "" };
				if (name.ends_with(ShellSuffix())) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				auto* property = LightingPropertyOf(a_geometry);
				if (property && IsPBRProperty(property)) {
					piece.piece.diffusePaths.push_back(TexturePath(static_cast<PBRMaterialLayout*>(property->material)->diffuseTexture));
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			if (piece.piece.diffusePaths.empty()) {
				if (settings.verboseLogging && loggedNonPBRArmor_.insert(piece.armor).second) {
					logger::info("armor {:08X} ({}) has no PBR geometry; left alone", piece.armor, piece.armorName);
				}
				continue;
			}
			for (const auto& resolved : Resolve(piece.piece, loaded)) {
				// An isolated recipe is applied alone, so nothing another recipe
				// would replace or bind first is missing from it.
				if (view_.Isolating() && (!resolved.recipe || resolved.recipe->id != view_.isolateRecipe)) {
					continue;
				}
				AppliedRecipe applied;
				applied.recipe = resolved.recipe;
				applied.key = resolved.key;
				applied.priority = resolved.priority;
				applied.graph = GraphFor(*resolved.recipe);
				if (!applied.graph) {
					continue;
				}
				applied.signals = std::make_unique<SignalState>(*applied.graph);
				applied.environment = std::make_unique<ActorEnvironment>(a_actor, WornEnchantment(a_actor, armor));
				applied.startMS = NowMS();
				if (const auto carried = carriedTimes_.find({ a_actor->GetFormID(), resolved.recipe->id }); carried != carriedTimes_.end()) {
					const float speed = settings.animationSpeed * resolved.recipe->clock.speed;
					if (applied.startMS - carried->second.retiredMS <= kCarryWindowMS && speed > 0.0f) {
						applied.startMS -= static_cast<std::uint32_t>(carried->second.seconds / speed * 1000.0f);
						applied.lastTime = carried->second.seconds;
					}
					carriedTimes_.erase(carried);
				}
				piece.recipes.push_back(std::move(applied));
			}
			if (piece.recipes.empty()) {
				if (settings.verboseLogging) {
					logger::info("armor {:08X} ({}): no recipe resolved{}", piece.armor, piece.armorName, piece.piece.effectShader ? "" : " (unenchanted)");
				}
				continue;
			}
			ApplyRecipes(a_actor, piece, clone);
			out.push_back(std::move(piece));
		}
		return out;
	}

	void Manager::ApplyRecipes(RE::Actor* a_actor, AppliedPiece& a_piece, RE::NiAVObject* a_clone)
	{
		const auto& settings = GetSettings();
		// Collected first: shells attach siblings while applying, and a live walk
		// would visit them.
		std::vector<std::pair<RE::BSGeometry*, RE::BSLightingShaderProperty*>> targets;
		std::unordered_set<RE::BSLightingShaderProperty*>                      seen;
		RE::BSVisit::TraverseScenegraphGeometries(a_clone, [&](RE::BSGeometry* a_geometry) {
			const std::string_view name{ a_geometry->name.c_str() ? a_geometry->name.c_str() : "" };
			if (name.ends_with(ShellSuffix())) {
				return RE::BSVisit::BSVisitControl::kContinue;
			}
			auto* property = LightingPropertyOf(a_geometry);
			if (property && seen.insert(property).second && IsPBRProperty(property)) {
				targets.emplace_back(a_geometry, property);
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});

		for (std::size_t r = 0; r < a_piece.recipes.size(); ++r) {
			auto& applied = a_piece.recipes[r];
			if (settings.verboseLogging) {
				logger::info("armor {:08X} ({}) {} actor {:08X}: recipe {} by {} (priority {})", a_piece.armor, a_piece.armorName, a_piece.firstPerson ? "1st" : "3rd", a_actor->GetFormID(),
					applied.recipe->id, applied.key.ToString(), applied.priority);
			}
			applied.signals->Tick(*applied.environment, { 0.0f, 0.0f });
			applied.replaced = ReplacedBy(std::span{ a_piece.recipes }.subspan(r + 1));
			auto* root = a_actor->Get3D(a_piece.firstPerson);
			for (const auto& [geometry, property] : targets) {
				if (!ApplyGeometry(a_actor, root, applied, geometry, property)) {
					break;
				}
			}
			// One light per recipe, third person only.
			if (!a_piece.firstPerson && !applied.replaced.light.empty()) {
				if (settings.verboseLogging) {
					logger::info("  recipe {}: light replaced by recipe {}", applied.recipe->id, applied.replaced.light);
				}
			} else if (!a_piece.firstPerson) {
				for (std::size_t i = 0; i < applied.recipe->outputs.size(); ++i) {
					const auto* light = Get<LightOutput>(applied.recipe->outputs[i]);
					if (!light) {
						continue;
					}
					std::vector<RE::BSGeometry*> geometries;
					for (const auto& g : applied.geometries) {
						geometries.push_back(g.geometry.get());
					}
					const auto placements = PlaceLights(light->bones, geometries, a_actor->Get3D(false), applied.signals->Resolve(light->offset));
					applied.light = LightBinding::Create(placements, light->shadow);
					applied.lightOutput = i;
					if (settings.verboseLogging) {
						logger::info("  recipe {}: {}", applied.recipe->id, applied.light ? applied.light->Describe() : "light not created");
					}
					break;
				}
			}
		}
		std::erase_if(a_piece.recipes, [](const AppliedRecipe& r) { return r.geometries.empty() && !r.light; });
	}

	// A cheap check that the material really has CS's layout (NOTES 3).
	bool Manager::LayoutSanityCheck(RE::BSLightingShaderProperty* a_property)
	{
		if (layoutVerified_) {
			return true;
		}
		auto* material = static_cast<PBRMaterialLayout*>(a_property->material);
		const std::array textures{ material->rmaosTexture.get(), material->emissiveTexture.get(), material->displacementTexture.get(), material->featuresTexture0.get(), material->featuresTexture1.get() };
		for (auto* texture : textures) {
			if (!texture || !netimmerse_cast<RE::NiSourceTexture*>(static_cast<RE::NiTexture*>(texture))) {
				logger::error("PBR material layout check failed on {}: texture slots are not all NiSourceTexture; emissive path disabled", a_property->name.c_str());
				emissivePathEnabled_ = false;
				return false;
			}
		}
		layoutVerified_ = true;
		logger::info("PBR material layout check passed");
		return true;
	}

	bool Manager::ApplyGeometry(RE::Actor* a_actor, RE::NiAVObject* a_root, AppliedRecipe& a_applied, RE::BSGeometry* a_geometry, RE::BSLightingShaderProperty* a_property)
	{
		const auto& settings = GetSettings();
		if (!LayoutSanityCheck(a_property)) {
			return false;
		}
		if (!a_property->emissiveColor) {
			if (settings.verboseLogging) {
				logger::info("skip geometry {}: property has no emissive colour storage", a_geometry->name.c_str());
			}
			return true;
		}
		const auto&   recipe = *a_applied.recipe;
		BoundGeometry bound;
		bound.geometry = RE::NiPointer{ a_geometry };
		bound.name = a_geometry->name.c_str() ? a_geometry->name.c_str() : "";
		bound.inputs.material = MaterialInputs::From(*static_cast<PBRMaterialLayout*>(a_property->material));
		bound.inputs.geometry = RE::NiPointer{ a_geometry };
		bound.inputs.root = RE::NiPointer{ a_root };
		const auto& inputs = bound.inputs;
		std::string outputsLog;

		for (std::size_t i = 0; i < recipe.outputs.size(); ++i) {
			const auto* material = Get<MaterialOutput>(recipe.outputs[i]);
			if (!material) {
				continue;
			}
			BoundOutput output;
			output.index = i;
			GeometryIdentity identity{ std::nullopt, bound.name, inputs.material.diffuse ? TexturePath(inputs.material.diffuse) : "" };
			if (!Matches(material->selector, identity)) {
				output.problem = "selector did not match";
			} else if (const auto replaced = a_applied.replaced.slots.find(material->slot); replaced != a_applied.replaced.slots.end()) {
				output.problem = std::format("replaced by recipe {}", replaced->second);
			} else {
				if (material->surface == Surface::kShell && !bound.shell) {
					bound.shell = ShellBinding::Create(a_geometry, a_property, recipe.shell);
					if (!bound.shell) {
						output.problem = "shell could not be created";
					}
				}
				if (material->surface == Surface::kMaterial && !bound.material) {
					bound.material = MaterialBinding::Install(a_geometry, a_property, settings.uniqueMaterial);
					if (!bound.material) {
						output.problem = "material binding failed";
					}
				}
				if (output.problem.empty()) {
					output.problem = TargetFor(bound, material->surface)->Problem(material->slot);
				}
				if (output.problem.empty()) {
					output.stack = Compositor::GetSingleton()->Prepare(recipe, *material, inputs, settings.runtimeTextureSize, settings.glossMapSize);
					if (!output.stack) {
						output.problem = "the texture lab is unavailable";
					} else {
						for (const auto& d : output.stack->Diagnostics()) {
							logger::warn("recipe {} output {} on '{}': {}: {}", recipe.id, i, bound.name, d.where, d.message);
						}
					}
				}
			}
			outputsLog += std::format("{}{}->{}{}", outputsLog.empty() ? "" : ", ", SlotName(material->slot), material->surface == Surface::kShell ? "shell" : "material",
				output.problem.empty() ? (output.stack && output.stack->Animated() ? " (animated)" : " (static)") : std::format(" [{}]", output.problem));
			bound.outputs.push_back(std::move(output));
		}
		// A geometry nothing binds on is recorded too, so a recipe stays on
		// the piece while its outputs are cleared or not yet added and the
		// studio shows it an empty board.
		if (settings.verboseLogging) {
			logger::info("apply recipe {} armor actor {:08X} geometry '{}' material={} {} outputs: {}", recipe.id, a_actor->GetFormID(), bound.name,
				bound.material ? (bound.material->Private() ? "private" : "shared") : "untouched", bound.shell ? bound.shell->Describe() : "no shell", outputsLog);
		}
		a_applied.geometries.push_back(std::move(bound));
		return true;
	}

	// ------------------------------------------------------------- editing

	void Manager::WithRecipeRetired(std::string_view a_id, const std::function<void()>& a_action)
	{
		std::vector<RE::FormID> wearers;
		for (const auto& [actorID, state] : applied_) {
			for (const auto& piece : state.pieces) {
				if (std::ranges::any_of(piece.recipes, [&](const AppliedRecipe& r) { return r.recipe && r.recipe->id == a_id; })) {
					wearers.push_back(actorID);
					break;
				}
			}
		}
		for (const auto actorID : wearers) {
			Retire(actorID);
		}
		a_action();
		for (const auto actorID : wearers) {
			QueueRefresh(actorID);
		}
	}

	void Manager::EditRecipe(std::string a_id, std::function<void(Recipe&)> a_edit)
	{
		PostTask([this, id = std::move(a_id), edit = std::move(a_edit)] {
			WithRecipeRetired(id, [&] {
				auto* recipe = MutableRecipe(id);
				if (!recipe) {
					logger::warn("edit: recipe {} is not loaded", id);
					return;
				}
				// A refused edit leaves the recipe as it was and pushes no step.
				Recipe before = *recipe;
				edit(*recipe);
				if (!(*recipe == before)) {
					histories_[id].Push(std::move(before));
				}
				for (const auto& d : Revalidate(id)) {
					if (d.severity == Severity::kError) {
						logger::error("recipe {} {}: {}", id, d.where, d.message);
					} else {
						logger::warn("recipe {} {}: {}", id, d.where, d.message);
					}
				}
			});
		});
	}

	void Manager::UndoRecipe(std::string a_id)
	{
		PostTask([this, id = std::move(a_id)] {
			WithRecipeRetired(id, [&] {
				auto* recipe = MutableRecipe(id);
				if (!recipe) {
					return;
				}
				if (auto restored = histories_[id].Undo(*recipe)) {
					*recipe = std::move(*restored);
					Revalidate(id);
				}
			});
		});
	}

	void Manager::RedoRecipe(std::string a_id)
	{
		PostTask([this, id = std::move(a_id)] {
			WithRecipeRetired(id, [&] {
				auto* recipe = MutableRecipe(id);
				if (!recipe) {
					return;
				}
				if (auto restored = histories_[id].Redo(*recipe)) {
					*recipe = std::move(*restored);
					Revalidate(id);
				}
			});
		});
	}

	void Manager::SaveRecipe(std::string a_id)
	{
		PostTask([id = std::move(a_id)] {
			const auto saved = WornEnchantmentPBR::SaveRecipe(id);
			if (!saved) {
				logger::error("recipe {}: save failed ({})", id, saved.error());
			}
		});
	}

	void Manager::RevertRecipe(std::string a_id)
	{
		PostTask([this, id = std::move(a_id)] {
			WithRecipeRetired(id, [&] {
				if (WornEnchantmentPBR::RevertRecipe(id)) {
					histories_.erase(id);
					logger::info("recipe {}: reverted to its file", id);
				}
			});
		});
	}

	void Manager::ReloadRecipes()
	{
		PostTask([this] {
			std::vector<RE::FormID> ids;
			for (const auto& [id, state] : applied_) {
				ids.push_back(id);
			}
			for (const auto id : ids) {
				Retire(id);
			}
			histories_.clear();
			LoadRecipes();
			QueueLoadedActorRefreshes();
		});
	}

	void Manager::NewRecipe(std::string a_id, RE::FormID a_armor)
	{
		PostTask([this, id = std::move(a_id), armorID = a_armor] {
			const auto* armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(armorID);
			if (!armor) {
				logger::warn("new recipe {}: armor {:08X} is not loaded", id, armorID);
				return;
			}
			RecipeKey key;
			key.kind = KeyKind::kArmor;
			key.form.key = FormKeyFor(*armor);
			const auto editorID = EditorIdOf(*armor);
			key.form.text = editorID.empty() ? key.form.key->ToString() : editorID;
			std::vector<RE::FormID> ids;
			for (const auto& [actorID, state] : applied_) {
				ids.push_back(actorID);
			}
			for (const auto actorID : ids) {
				Retire(actorID);
			}
			[[maybe_unused]] const bool made = WornEnchantmentPBR::NewRecipe(id, std::move(key));
			QueueLoadedActorRefreshes();
		});
	}

	// -------------------------------------------------------------- retire

	void Manager::Retire(RE::FormID a_actorID)
	{
		const auto it = applied_.find(a_actorID);
		if (it == applied_.end()) {
			return;
		}
		std::size_t recipes = 0;
		const auto  now = NowMS();
		for (const auto& p : it->second.pieces) {
			recipes += p.recipes.size();
			for (const auto& applied : p.recipes) {
				if (applied.recipe) {
					carriedTimes_[{ a_actorID, applied.recipe->id }] = CarriedTime{ applied.lastTime, now };
				}
			}
		}
		applied_.erase(it);  // bindings restore in their destructors, outputs before shells and materials
		TextureLab::GetSingleton()->InvalidatePreviews();
		UnwatchAnimationEvents(RE::TESForm::LookupByID<RE::Actor>(a_actorID));
		if (GetSettings().verboseLogging) {
			logger::info("actor {:08X}: retired {} recipe(s)", a_actorID, recipes);
		}
	}

	// ---------------------------------------------------------------- tick

	void Manager::OnFrame()
	{
		FireDueFinalizes();
		if (applied_.empty()) {
			return;
		}
		const auto now = NowMS();
		if (now - lastTickMS_ >= GetSettings().TickIntervalMS()) {
			lastTickMS_ = now;
			Tick(now);
		}
	}

	void Manager::Fire(RE::FormID a_actorID, const EventRecord& a_event)
	{
		const auto it = applied_.find(a_actorID);
		if (it == applied_.end()) {
			return;
		}
		for (auto& piece : it->second.pieces) {
			for (auto& applied : piece.recipes) {
				applied.signals->Fire(a_event, applied.lastTime);
			}
		}
	}

	void Manager::Tick(std::uint32_t a_nowMS)
	{
		const auto& settings = GetSettings();
		Compositor::GetSingleton()->BeginTick();
		TextureLab::GetSingleton()->RenderPreviews();  // the menu's thumbnails, on this thread only
		// Leaving freeze: every recipe's clock resumes from the scrub, not from
		// where the real clock ran on to meanwhile.
		const bool resuming = frozenLastTick_ && !view_.freeze;
		frozenLastTick_ = view_.freeze;
		for (auto it = applied_.begin(); it != applied_.end();) {
			for (auto& piece : it->second.pieces) {
				for (auto& applied : piece.recipes) {
					const float speed = settings.animationSpeed * view_.speed * applied.recipe->clock.speed;
					if (resuming && speed > 0.0f) {
						applied.startMS = a_nowMS - static_cast<std::uint32_t>(view_.scrubSeconds / speed * 1000.0f);
					}
					const float time = view_.freeze ? view_.scrubSeconds : static_cast<float>(a_nowMS - applied.startMS) * 0.001f * speed;
					const float delta = std::max(0.0f, time - applied.lastTime);
					TickRecipe(applied, time, delta);
					applied.lastTime = time;
				}
				std::erase_if(piece.recipes, [](const AppliedRecipe& r) { return r.geometries.empty() && !r.light; });
			}
			std::erase_if(it->second.pieces, [](const AppliedPiece& p) { return p.recipes.empty(); });
			it = it->second.pieces.empty() ? applied_.erase(it) : std::next(it);
		}
	}

	void Manager::TickRecipe(AppliedRecipe& a_applied, float a_time, float a_delta)
	{
		const auto& recipe = *a_applied.recipe;
		// The signal state integrates forward (pulse phase, smoothing, trigger
		// ages), so a scrub backwards, or a jump, rebuilds it from the start
		// and advances it to the scrubbed moment in one step; every signal then
		// stands where a straight run to that time would have left it.
		if (view_.freeze && a_time + 0.001f < a_applied.lastTime) {
			a_applied.signals = std::make_unique<SignalState>(*a_applied.graph);
			a_delta = a_time;
		}
		auto& signals = *a_applied.signals;
		signals.Tick(*a_applied.environment, { a_time, a_delta });
		const auto& view = view_;
		const bool  anyLayerHidden = view.isolateLayer >= 0 || !view.muted.empty();

		std::erase_if(a_applied.geometries, [&](BoundGeometry& a_bound) {
			if ((a_bound.material && !a_bound.material->StillOwned()) || (a_bound.shell && !a_bound.shell->StillOwned())) {
				logger::info("dropping '{}': its material or shell was replaced by another system", a_bound.name);
				return true;
			}
			for (auto& output : a_bound.outputs) {
				if (!output.stack) {
					continue;
				}
				const auto* material = Get<MaterialOutput>(recipe.outputs[output.index]);
				if (!material) {
					continue;
				}
				const bool  shown = view.OutputShown(recipe.id, output.index);
				LayerFilter filter;
				if (shown && anyLayerHidden) {
					filter = HiddenLayers(view, recipe.id, output.index, material->stack.size());
				}
				Compositor::GetSingleton()->Render(*output.stack, signals, a_time, filter);
				auto* target = TargetFor(a_bound, material->surface);
				if (!target) {
					continue;
				}
				WriteSlot(*target, *material, signals, output.stack->Texture(), shown);
			}
			if (a_bound.shell) {
				const auto& pose = recipe.shell.pose;
				a_bound.shell->Pose(signals.Resolve(pose.inflate), signals.Resolve(recipe.shell.alpha), signals.Resolve(recipe.shell.rimPower), signals.Resolve(recipe.shell.emissive));
				a_bound.shell->SetVisible(view.RecipeShown(recipe.id));
			}
			return false;
		});

		if (a_applied.light && a_applied.lightOutput) {
			if (const auto* light = Get<LightOutput>(recipe.outputs[*a_applied.lightOutput])) {
				a_applied.light->Update(signals.Resolve(light->color), signals.Resolve(light->intensity), signals.Resolve(light->size), signals.Resolve(light->cutoff), view.OutputShown(recipe.id, *a_applied.lightOutput));
			}
		}
	}

	// ----------------------------------------------------------- reporting

	Manager::Status Manager::GetStatus() const
	{
		Status s;
		s.emissivePath = emissivePathEnabled_;
		s.layoutVerified = layoutVerified_;
		s.runtimeLab = TextureLab::GetSingleton()->Available();
		s.actors = static_cast<std::uint32_t>(applied_.size());
		for (const auto& [id, state] : applied_) {
			s.pieces += static_cast<std::uint32_t>(state.pieces.size());
			for (const auto& piece : state.pieces) {
				s.recipes += static_cast<std::uint32_t>(piece.recipes.size());
				for (const auto& applied : piece.recipes) {
					s.geometries += static_cast<std::uint32_t>(applied.geometries.size());
					for (const auto& g : applied.geometries) {
						s.shells += g.shell ? 1 : 0;
					}
					s.lights += applied.light ? 1 : 0;
				}
			}
		}
		s.tickMS = GetSettings().TickIntervalMS();
		return s;
	}

	Manager::Snapshot Manager::TakeSnapshot() const
	{
		Snapshot out;
		for (const auto& [actorID, state] : applied_) {
			const auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorID);
			for (const auto& piece : state.pieces) {
				Snapshot::PieceRow row;
				row.actorID = actorID;
				row.actorName = actor && actor->GetName() ? actor->GetName() : "?";
				row.armorID = piece.armor;
				row.armorName = piece.armorName;
				row.firstPerson = piece.firstPerson;
				for (const auto& applied : piece.recipes) {
					Snapshot::RecipeRow r;
					r.id = applied.recipe->id;
					r.key = applied.key.ToString();
					r.priority = applied.priority;
					r.time = applied.lastTime;
					r.dirty = IsDirty(r.id);
					r.shellMaterial = applied.recipe->shell.material;
					r.lightOutput = applied.lightOutput;
					r.lightRow = Studio::LightRowOf(*applied.recipe);
					r.shellRow = Studio::ShellRowOf(*applied.recipe);
					if (const auto history = histories_.find(r.id); history != histories_.end()) {
						r.undoDepth = history->second.UndoDepth();
						r.redoDepth = history->second.RedoDepth();
					}
					for (const auto& mask : applied.recipe->masks) {
						r.masks.push_back(mask.name);
					}
					if (const auto origin = OriginOf(*applied.recipe)) {
						r.problems.assign(origin->diagnostics.begin(), origin->diagnostics.end());
					}
					const auto references = Studio::CountReferences(*applied.recipe);
					for (std::size_t i = 0; i < applied.graph->Size(); ++i) {
						const auto& signal = applied.graph->At(i);
						Snapshot::SignalRow row;
						row.name = signal.name;
						row.kind = KindName(signal);
						row.type = applied.graph->TypeOf(i);
						row.value = applied.signals->ValueOf(i);
						row.inert = applied.graph->Inert(i);
						row.problem = row.inert ? InertReason(*applied.graph, signal.name) : std::string{};
						if (const auto* declared = applied.recipe->FindSignal(signal.name)) {
							if (const auto* constant = Get<ConstantSignal>(declared->kind)) {
								row.constant = constant->value;
							}
							if (const auto* expr = Get<ExprSignal>(declared->kind)) {
								row.text = expr->text;
							}
							if (const auto* trigger = Get<TriggerSignal>(declared->kind)) {
								// The bus id a fire button posts: an event glob as written, or
								// a plugin id; a `when` source fires from its signal alone.
								if (const auto* event = Get<EventSource>(trigger->source)) {
									row.event = event->event;
								} else if (const auto* plugin = Get<PluginSource>(trigger->source)) {
									row.event = plugin->id;
								}
							}
							row.curve = declared->curve ? declared->curve->text : "";
						}
						if (const auto count = references.signals.find(row.name); count != references.signals.end()) {
							row.references = count->second;
						}
						r.signals.push_back(std::move(row));
					}
					for (const auto& curve : applied.recipe->curves) {
						const auto count = references.curves.find(curve.name);
						r.curves.push_back({ curve.name, curve.text, count != references.curves.end() ? count->second : 0 });
					}
					for (const auto& g : applied.geometries) {
						Snapshot::GeometryRow gr;
						gr.name = g.name;
						gr.privateMaterial = g.material && g.material->Private();
						gr.shell = g.shell ? g.shell->Describe() : "";
						gr.materialSlots = g.material ? SlotRows(*g.material) : std::vector<Snapshot::SlotRow>{};
						gr.shellSlots = g.shell ? SlotRows(*g.shell) : std::vector<Snapshot::SlotRow>{};
						auto* compositor = Compositor::GetSingleton();
						for (const auto& source : applied.recipe->sources) {
							Snapshot::ImageRow row;
							row.name = source.name;
							row.kind = DescribeSource(source.kind);
							if (const auto prepared = compositor->InspectSource(*applied.recipe, source.name, g.inputs)) {
								row.texture = prepared->texture.get();
								row.channel = prepared->sampling.channel;
								row.animated = prepared->animated;
								row.problem = prepared->problem;
							}
							gr.sources.push_back(std::move(row));
						}
						for (const auto& mask : applied.recipe->masks) {
							Snapshot::ImageRow row;
							row.name = mask.name;
							row.kind = mask.text;
							if (const auto prepared = compositor->InspectMask(*applied.recipe, mask.name, g.inputs)) {
								row.texture = prepared->texture.get();
								row.channel = prepared->channel;
								row.animated = prepared->animated;
								row.problem = prepared->problem;
							}
							gr.masks.push_back(std::move(row));
						}
						for (const auto& o : g.outputs) {
							const auto* material = Get<MaterialOutput>(applied.recipe->outputs[o.index]);
							Snapshot::OutputRow orow;
							orow.index = o.index;
							orow.target = material ? (material->surface == Surface::kShell ? "shell" : "material") : "light";
							orow.light = !material;
							if (material) {
								orow.surface = material->surface;
								orow.slot = material->slot;
								orow.slotName = std::string{ SlotName(material->slot) };
								orow.replace = material->replace;
							}
							orow.animated = o.stack && o.stack->Animated();
							orow.size = o.stack ? o.stack->Size() : 0;
							orow.problem = o.problem;
							orow.texture = o.stack ? o.stack->Texture() : nullptr;
							if (material) {
								const auto& sc = material->scalars;
								const auto& sig = *applied.signals;
								if (sc.strength) {
									orow.scalars.push_back({ "strength", sig.Resolve(*sc.strength), ParamText(*sc.strength) });
								}
								if (sc.scale) {
									orow.scalars.push_back({ "scale", sig.Resolve(*sc.scale), ParamText(*sc.scale) });
								}
								if (sc.color) {
									orow.scalars.push_back({ "color", sig.Resolve(*sc.color), Vec3ParamText(*sc.color) });
								}
								if (sc.weight) {
									orow.scalars.push_back({ "weight", sig.Resolve(*sc.weight), ParamText(*sc.weight) });
								}
								for (const auto& [name, param] : { std::pair{ "screenSpaceScale", &sc.screenSpaceScale }, std::pair{ "logMicrofacetDensity", &sc.logMicrofacetDensity },
										 std::pair{ "microfacetRoughness", &sc.microfacetRoughness }, std::pair{ "densityRandomization", &sc.densityRandomization },
										 std::pair{ "roughness", &sc.roughness }, std::pair{ "level", &sc.level }, std::pair{ "thickness", &sc.thickness } }) {
									if (*param) {
										orow.scalars.push_back({ name, sig.Resolve(**param), ParamText(**param) });
									}
								}
								// Every layer of the file, with the compositor's verdict where it has one.
								for (const auto& layer : material->stack) {
									Snapshot::LayerRow lrow;
									lrow.source = LayerSourceText(layer.source);
									lrow.mask = layer.mask ? "@" + layer.mask->name : "";
									lrow.blend = std::string{ BlendName(layer.blend) };
									lrow.opacity = applied.signals->Resolve(layer.opacity);
									lrow.opacityText = ParamText(layer.opacity);
									lrow.color = layer.color ? Vec3ParamText(*layer.color) : "";
									lrow.curve = layer.curve ? layer.curve->text : "";
									lrow.channels = layer.channels.ToString();
									orow.layers.push_back(std::move(lrow));
								}
								if (o.stack) {
									for (const auto& prepared : o.stack->Layers()) {
										if (prepared.index < orow.layers.size() && prepared.source) {
											orow.layers[prepared.index].texture = prepared.source->texture.get();
										}
									}
									for (const auto& d : o.stack->Diagnostics()) {
										// "layer N" rows carry their own problem text.
										if (d.where.starts_with("layer ")) {
											const auto at = std::strtoul(d.where.c_str() + 6, nullptr, 10);
											if (at < orow.layers.size() && orow.layers[at].problem.empty()) {
												orow.layers[at].problem = d.message;
											}
										}
									}
								}
							}
							gr.outputs.push_back(std::move(orow));
						}
						r.geometries.push_back(std::move(gr));
					}
					r.light = applied.light ? applied.light->Describe() : "";
					row.recipes.push_back(std::move(r));
				}
				out.pieces.push_back(std::move(row));
			}
		}
		return out;
	}
}
