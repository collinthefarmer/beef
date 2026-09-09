#include "Manager.h"

#include "Edits.h"
#include "EngineForms.h"
#include "Paint.h"

#include <random>
#include "Studio.h"
#include "Events.h"
#include "RecipeStore.h"
#include "Settings.h"

namespace WornEnchantmentPBR
{
	namespace
	{
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

		std::unordered_map<std::string, std::string> InertReasons(const SignalGraph& a_graph)
		{
			std::unordered_map<std::string, std::string> reasons;
			for (const auto& d : a_graph.Diagnostics()) {
				if (d.where.starts_with("signal ")) {
					reasons.emplace(d.where.substr(7), d.message);
				}
			}
			return reasons;
		}

		std::vector<Studio::SlotRow> SlotRows(const SlotTarget& a_target)
		{
			std::vector<Studio::SlotRow> rows;
			for (const auto& s : a_target.Slots()) {
				rows.push_back({ s.slot, s.original, s.written, a_target.Problem(s.slot) });
			}
			return rows;
		}

	}

	Manager* Manager::GetSingleton()
	{
		static Manager singleton;
		return &singleton;
	}

	namespace
	{
		PlacedOutput* OutputAt(Placement& a_placement, std::size_t a_index)
		{
			for (auto& output : a_placement.outputs) {
				if (output.index == a_index) {
					return &output;
				}
			}
			return nullptr;
		}

		const PlacedOutput* OutputAt(const Placement& a_placement, std::size_t a_index)
		{
			for (const auto& output : a_placement.outputs) {
				if (output.index == a_index) {
					return &output;
				}
			}
			return nullptr;
		}

		std::optional<std::size_t> MergeOf(const GeometryPlan& a_plan, Contribution a_contribution)
		{
			for (const auto& slot : a_plan.slots) {
				for (std::size_t i = 0; i < slot.chain.size(); ++i) {
					if (slot.chain[i] == a_contribution) {
						return i;
					}
				}
			}
			return std::nullopt;
		}

		SlotTarget* TargetFor(GeometryBinding& a_bound, Surface a_surface)
		{
			if (a_surface == Surface::kShell) {
				return a_bound.shell.get();
			}
			return a_bound.material.get();
		}

		struct SlotWrite
		{
			Slot                                slot = Slot::kEmissive;
			RE::NiSourceTexture*                texture = nullptr;
			bool                                shown = false;
			std::array<float, kScalarFieldCount> scalars{};
			Vec3                                color{ 1.0f, 1.0f, 1.0f };

			[[nodiscard]] float Of(ScalarField a_field) const { return scalars[static_cast<std::size_t>(a_field)]; }
			[[nodiscard]] float Shown(ScalarField a_field) const { return shown ? Of(a_field) : 0.0f; }
		};

		SlotWrite EmptyWrite(Slot a_slot)
		{
			SlotWrite write;
			write.slot = a_slot;
			for (const auto field : ScalarsOf(a_slot)) {
				write.scalars[static_cast<std::size_t>(field)] = ScalarFallback(field);
			}
			const float fallback = ScalarFallback(ScalarField::kColor);
			write.color = Vec3{ fallback, fallback, fallback };
			return write;
		}

		void TakeScalar(SlotWrite& a_write, ScalarField a_field, const SlotScalars& a_scalars, const SignalState& a_signals)
		{
			if (a_field == ScalarField::kColor) {
				if (a_scalars.color) {
					a_write.color = a_signals.Resolve(*a_scalars.color);
				}
			} else if (const auto* param = ScalarOf(a_scalars, a_field); param && *param) {
				a_write.scalars[static_cast<std::size_t>(a_field)] = a_signals.Resolve(**param);
			}
		}

		void WriteSlot(SlotTarget& a_target, const SlotWrite& a_write)
		{
			a_target.WriteTexture(a_write.slot, a_write.shown ? a_write.texture : nullptr);
			switch (a_write.slot) {
			case Slot::kEmissive:
				a_target.WriteEmissive(Vec3{ 1.0f, 1.0f, 1.0f }, a_write.Shown(ScalarField::kStrength));
				break;
			case Slot::kFuzz:
				a_target.WriteFuzz(a_write.color, a_write.Shown(ScalarField::kWeight));
				break;
			case Slot::kHeight:
				a_target.WriteHeightScale(a_write.Shown(ScalarField::kScale));
				break;
			case Slot::kGlint:
				a_target.WriteGlint(a_write.Of(ScalarField::kScreenSpaceScale), a_write.Of(ScalarField::kLogMicrofacetDensity), a_write.Of(ScalarField::kMicrofacetRoughness), a_write.Of(ScalarField::kDensityRandomization), a_write.shown);
				break;
			case Slot::kCoat:
				a_target.WriteCoat(a_write.Of(ScalarField::kRoughness), a_write.Shown(ScalarField::kLevel));
				break;
			case Slot::kSubsurface:
				a_target.WriteSubsurface(a_write.color, a_write.Shown(ScalarField::kThickness));
				break;
			default:
				break;
			}
		}

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

		EventRecord EquipEvent(const std::vector<AppliedPiece>& a_pieces)
		{
			EventRecord record;
			record.id = "equip";
			for (const auto& piece : a_pieces) {
				for (const auto& bound : piece.geometries) {
					if (bound.geometry) {
						const auto& c = bound.geometry->worldBound.center;
						record.payload.position = Vec3{ c.x, c.y, c.z };
						return record;
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
		applied_.clear();
		loggedNonPBRArmor_.clear();
		carriedTimes_.clear();
		Compositor::GetSingleton()->ClearMeshes();
		Compositor::GetSingleton()->ClearMaterials();
		TextureLab::GetSingleton()->Clear();
		logger::info("cleared {} actor states", count);
	}

	void Manager::Isolate(std::string a_recipe, int a_output, int a_layer)
	{
		PostTask([this, recipe = std::move(a_recipe), a_output, a_layer] {
			const bool changed = view_.isolateRecipe != recipe;
			view_.isolateRecipe = recipe;
			view_.isolateOutput = a_output;
			view_.isolateLayer = a_layer;
			if (changed) {
				ReapplyAll();
			}
		});
	}

	void Manager::PinRecipe(Studio::PieceRef a_piece, std::string a_recipeID)
	{
		PostTask([this, a_piece, id = std::move(a_recipeID)] {
			std::optional<Studio::Pin> pin;
			if (!id.empty()) {
				const auto loaded = LoadedRecipes();
				if (std::ranges::find(loaded, id, &Recipe::id) == loaded.end()) {
					logger::warn("pin: recipe {} is not loaded", id);
					return;
				}
				pin = Studio::Pin{ a_piece, id };
			}
			if (view_.pin == pin) {
				return;
			}
			WithListMoved([&] { view_.pin = pin; });
			if (pin) {
				logger::info("pin: {} shown on armor {:08X} of actor {:08X} ({}) while viewed", id, a_piece.armorID, a_piece.actorID, a_piece.firstPerson ? "1st" : "3rd");
			} else {
				logger::info("pin: cleared");
			}
		});
	}

	void Manager::UpdateView(std::function<void(Studio::View&)> a_change)
	{
		PostTask([this, change = std::move(a_change)] { change(view_); });
	}

	void Manager::ReapplyAll()
	{
		PostTask([this] {
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
		});
	}

	void Manager::RetireAll()
	{
		PostTask([this] {
			std::vector<RE::FormID> ids;
			for (const auto& [id, state] : applied_) {
				ids.push_back(id);
			}
			for (const auto id : ids) {
				QueueRetire(id);
			}
		});
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
		MatchRecipes(a_actor, state);
		std::erase_if(state.pieces, [](const AppliedPiece& a_piece) { return a_piece.matches.empty(); });
		TextureLab::GetSingleton()->InvalidatePreviews();
		if (state.pieces.empty()) {
			return;
		}
		PlaceInstances(a_actor, state);
		PlaceLightsOf(a_actor, state);
		if (settings.verboseLogging) {
			logger::info("actor {:08X} ({}): {} piece(s), {} recipe(s) applied", actorID, a_actor->GetName(), state.pieces.size(), state.instances.size());
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
		auto*                               root = a_actor->Get3D(a_firstPerson);
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
			piece.enchantment = WornEnchantment(a_actor, armor);
			if (auto* magic = piece.enchantment) {
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
			std::unordered_set<RE::BSLightingShaderProperty*> seen;
			RE::BSVisit::TraverseScenegraphGeometries(clone, [&](RE::BSGeometry* a_geometry) {
				const std::string_view name{ a_geometry->name.c_str() ? a_geometry->name.c_str() : "" };
				if (name.ends_with(ShellSuffix())) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				auto* property = LightingPropertyOf(a_geometry);
				if (!property || !IsPBRProperty(property) || !seen.insert(property).second) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				piece.piece.diffusePaths.push_back(TexturePath(static_cast<PBRMaterialLayout*>(property->material)->diffuseTexture));
				if (!LayoutSanityCheck(property)) {
					return RE::BSVisit::BSVisitControl::kStop;
				}
				if (!property->emissiveColor) {
					if (settings.verboseLogging) {
						logger::info("skip geometry {}: property has no emissive colour storage", name);
					}
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				GeometryBinding bound;
				bound.geometry = RE::NiPointer{ a_geometry };
				bound.property = RE::NiPointer{ property };
				bound.name = std::string{ name };
				bound.inputs.material = MaterialInputs::From(*static_cast<PBRMaterialLayout*>(property->material));
				bound.inputs.geometry = RE::NiPointer{ a_geometry };
				bound.inputs.root = RE::NiPointer{ root };
				piece.geometries.push_back(std::move(bound));
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			if (piece.piece.diffusePaths.empty()) {
				if (settings.verboseLogging && loggedNonPBRArmor_.insert(piece.armor).second) {
					logger::info("armor {:08X} ({}) has no PBR geometry; left alone", piece.armor, piece.armorName);
				}
				continue;
			}
			out.push_back(std::move(piece));
		}
		return out;
	}

	void Manager::MatchRecipes(RE::Actor* a_actor, ActorState& a_state)
	{
		const auto& settings = GetSettings();
		const auto  loaded = LoadedRecipes();
		for (auto& piece : a_state.pieces) {
			const Studio::PieceRef ref{ a_actor->GetFormID(), piece.armor, piece.firstPerson };
			for (const auto& resolved : Studio::ViewedRecipes(Resolve(piece.piece, loaded), piece.piece, ref, view_, loaded)) {
				if (!resolved.recipe) {
					continue;
				}
				const auto instance = InstanceFor(a_actor, a_state, *resolved.recipe, piece.enchantment);
				if (!instance) {
					continue;
				}
				a_state.instances[*instance].priority = std::max(a_state.instances[*instance].priority, resolved.priority);
				piece.matches.push_back(PieceMatch{ *instance, resolved.key, resolved.priority });
				if (settings.verboseLogging) {
					logger::info("armor {:08X} ({}) {} actor {:08X}: recipe {} by {} (priority {})", piece.armor, piece.armorName, piece.firstPerson ? "1st" : "3rd", a_actor->GetFormID(),
						resolved.recipe->id, resolved.key.ToString(), resolved.priority);
				}
			}
		}
	}

	std::optional<std::size_t> Manager::InstanceFor(RE::Actor* a_actor, ActorState& a_state, const Recipe& a_recipe, RE::MagicItem* a_enchantment)
	{
		const RE::FormID enchantment = a_enchantment ? a_enchantment->GetFormID() : 0;
		for (std::size_t i = 0; i < a_state.instances.size(); ++i) {
			const auto& instance = a_state.instances[i];
			if (instance.recipe && instance.recipe->id == a_recipe.id && instance.enchantment == enchantment) {
				return i;
			}
		}
		RecipeInstance instance;
		instance.recipe = &a_recipe;
		instance.enchantment = enchantment;
		instance.graph = GraphFor(a_recipe);
		if (!instance.graph) {
			return std::nullopt;
		}
		instance.signals = std::make_unique<SignalState>(*instance.graph);
		instance.environment = std::make_unique<ActorEnvironment>(a_actor, a_enchantment);
		instance.startMS = NowMS();
		if (const auto carried = carriedTimes_.find({ a_actor->GetFormID(), a_recipe.id }); carried != carriedTimes_.end()) {
			const float speed = GetSettings().animationSpeed * a_recipe.clock.speed;
			if (instance.startMS - carried->second.retiredMS <= kCarryWindowMS && speed > 0.0f) {
				instance.startMS -= static_cast<std::uint32_t>(carried->second.seconds / speed * 1000.0f);
				instance.lastTime = carried->second.seconds;
			}
			carriedTimes_.erase(carried);
		}
		a_state.instances.push_back(std::move(instance));
		return a_state.instances.size() - 1;
	}
	void Manager::PlaceInstances(RE::Actor* a_actor, ActorState& a_state)
	{
		for (auto& instance : a_state.instances) {
			instance.signals->Tick(*instance.environment, { 0.0f, 0.0f });
		}
		for (std::size_t p = 0; p < a_state.pieces.size(); ++p) {
			for (std::size_t g = 0; g < a_state.pieces[p].geometries.size(); ++g) {
				PlaceOnGeometry(a_actor, a_state, p, g);
			}
		}
	}

	void Manager::PlaceOnGeometry(RE::Actor* a_actor, ActorState& a_state, std::size_t a_piece, std::size_t a_geometry)
	{
		const auto&            settings = GetSettings();
		auto&                  piece = a_state.pieces[a_piece];
		auto&                  bound = piece.geometries[a_geometry];
		const GeometryIdentity identity{ std::nullopt, bound.name, bound.inputs.material.diffuse ? TexturePath(bound.inputs.material.diffuse) : "" };

		std::vector<PlacedRecipe> placed;
		for (const auto& match : piece.matches) {
			const auto&  instance = a_state.instances[match.instance];
			Placement    placement;
			placement.instance = match.instance;
			placement.piece = a_piece;
			placement.geometry = a_geometry;
			PlacedRecipe row;
			row.recipe = instance.recipe;
			row.priority = match.priority;
			for (std::size_t i = 0; i < instance.recipe->outputs.size(); ++i) {
				const auto* output = Get<SurfaceOutput>(instance.recipe->outputs[i]);
				if (!output) {
					continue;
				}
				PlacedOutput placedOutput;
				placedOutput.index = i;
				if (Matches(output->selector, identity)) {
					row.outputs.push_back(i);
				} else {
					placedOutput.problem = "selector did not match";
				}
				placement.outputs.push_back(std::move(placedOutput));
			}
			bound.placements.push_back(a_state.placements.size());
			a_state.placements.push_back(std::move(placement));
			placed.push_back(std::move(row));
		}
		bound.plan = PlanGeometry(placed);

		const auto instanceOf = [&](std::size_t a_placed) -> std::size_t { return a_state.placements[bound.placements[a_placed]].instance; };
		const auto outputAt = [&](Contribution a_c) { return OutputAt(a_state.placements[bound.placements[a_c.placed]], a_c.output); };

		for (const auto& slot : bound.plan.slots) {
			for (const auto& c : slot.replaced) {
				if (auto* output = outputAt(c)) {
					const auto replacer = ReplacerOf(slot, c);
					output->problem = std::format("replaced by recipe {}", replacer ? a_state.instances[instanceOf(*replacer)].recipe->id : std::string{});
				}
			}
		}

		std::optional<Contribution> shellTop;
		for (const auto& slot : bound.plan.slots) {
			if (slot.surface != Surface::kShell || slot.chain.empty()) {
				continue;
			}
			const auto top = slot.chain.back();
			if (!shellTop || placed[top.placed].priority > placed[shellTop->placed].priority) {
				shellTop = top;
			}
		}

		std::string outputsLog;
		for (const auto& slot : bound.plan.slots) {
			for (const auto& c : slot.chain) {
				auto* output = outputAt(c);
				if (!output) {
					continue;
				}
				const auto& instance = a_state.instances[instanceOf(c.placed)];
				const auto* material = Get<SurfaceOutput>(instance.recipe->outputs[c.output]);
				if (slot.surface == Surface::kShell && !bound.shell) {
					const auto owner = instanceOf(shellTop ? shellTop->placed : c.placed);
					bound.shell = ShellBinding::Create(bound.geometry.get(), bound.property.get(), a_state.instances[owner].recipe->shell);
					bound.shellOwner = owner;
					if (!bound.shell) {
						output->problem = "shell could not be created";
					}
				}
				if (slot.surface == Surface::kMaterial && !bound.material) {
					bound.material = MaterialBinding::Install(bound.geometry.get(), bound.property.get(), settings.uniqueMaterial);
					if (!bound.material) {
						output->problem = "material binding failed";
					}
				}
				if (output->problem.empty()) {
					auto* target = TargetFor(bound, slot.surface);
					output->problem = target ? target->Problem(slot.slot) : "the surface is not bound";
				}
				if (output->problem.empty()) {
					output->stack = Compositor::GetSingleton()->Prepare(*instance.recipe, *material, bound.inputs, TextureSize::Clamp(settings.runtimeTextureSize), TextureSize::Clamp(settings.glossMapSize));
					if (!output->stack) {
						output->problem = "the texture lab is unavailable";
					} else {
						for (const auto& d : output->stack->Diagnostics()) {
							logger::warn("recipe {} output {} on '{}': {}: {}", instance.recipe->id, c.output, bound.name, d.where, d.message);
						}
					}
				}
				outputsLog += std::format("{}{} {}->{}{}", outputsLog.empty() ? "" : ", ", instance.recipe->id, SlotName(slot.slot), SurfaceName(slot.surface),
					output->problem.empty() ? (output->stack && output->stack->Animated() ? " (animated)" : " (static)") : std::format(" [{}]", output->problem));
			}
		}
		if (settings.verboseLogging) {
			logger::info("apply armor {:08X} actor {:08X} geometry '{}' material={} {} outputs: {}", piece.armor, a_actor->GetFormID(), bound.name,
				bound.material ? (bound.material->Private() ? "private" : "shared") : "untouched", bound.shell ? bound.shell->Describe() : "no shell", outputsLog.empty() ? "none" : outputsLog);
		}
	}

	void Manager::PlaceLightsOf(RE::Actor* a_actor, ActorState& a_state)
	{
		const auto&               settings = GetSettings();
		std::vector<PlacedRecipe> placed;
		for (const auto& instance : a_state.instances) {
			placed.push_back(PlacedRecipe{ instance.recipe, instance.priority, {} });
		}
		const auto plan = PlanLights(placed);
		for (const auto& c : plan.replaced) {
			if (settings.verboseLogging) {
				logger::info("  recipe {}: light replaced by recipe {}", a_state.instances[c.placed].recipe->id,
					plan.replacer ? a_state.instances[plan.replacer->placed].recipe->id : std::string{});
			}
		}
		for (const auto& c : plan.shown) {
			auto&                        instance = a_state.instances[c.placed];
			std::vector<RE::BSGeometry*> geometries;
			for (const auto& placement : a_state.placements) {
				if (placement.instance != c.placed || a_state.pieces[placement.piece].firstPerson) {
					continue;
				}
				geometries.push_back(a_state.pieces[placement.piece].geometries[placement.geometry].geometry.get());
			}
			if (geometries.empty()) {
				continue;
			}
			const auto* light = Get<LightOutput>(instance.recipe->outputs[c.output]);
			const auto  placements = PlaceLights(light->bones, geometries, a_actor->Get3D(false), instance.signals->Resolve(light->offset));
			instance.light = LightBinding::Create(placements, light->shadow);
			instance.lightOutput = c.output;
			if (settings.verboseLogging) {
				logger::info("  recipe {}: {}", instance.recipe->id, instance.light ? instance.light->Describe() : "light not created");
			}
		}
	}

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

	void Manager::WithRecipeRetired(std::string_view a_id, const std::function<void()>& a_action)
	{
		std::vector<RE::FormID> wearers;
		for (const auto& [actorID, state] : applied_) {
			if (std::ranges::any_of(state.instances, [&](const RecipeInstance& a_instance) { return a_instance.recipe && a_instance.recipe->id == a_id; })) {
				wearers.push_back(actorID);
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

	void Manager::EditRecipe(std::string a_id, Studio::EditBatch a_edits)
	{
		PostTask([this, id = std::move(a_id), edits = std::move(a_edits)] { ApplyEdits(id, edits); });
	}

	void Manager::ApplyEdits(const std::string& a_id, const Studio::EditBatch& a_edits)
	{
		bool keysChanged = false;
		WithRecipeRetired(a_id, [&] {
			auto* recipe = MutableRecipe(a_id);
			if (!recipe) {
				logger::warn("edit: recipe {} is not loaded", a_id);
				return;
			}
			Recipe before = *recipe;
			if (const auto problem = Studio::Apply(*recipe, a_edits)) {
				logger::warn("edit refused: {} ({}: {})", Studio::Describe(a_edits), problem->where, problem->message);
				return;
			}
			keysChanged = recipe->keys != before.keys;
			if (!(*recipe == before)) {
				histories_[a_id].Push(std::move(before));
			}
			for (const auto& d : Revalidate(a_id)) {
				if (d.severity == Severity::kError) {
					logger::error("recipe {} {}: {}", a_id, d.where, d.message);
				} else {
					logger::warn("recipe {} {}: {}", a_id, d.where, d.message);
				}
			}
		});
		if (keysChanged) {
			QueueLoadedActorRefreshes();
		}
	}

	void Manager::WithListMoved(const std::function<void()>& a_action)
	{
		RetireEveryActor();
		a_action();
		QueueLoadedActorRefreshes();
	}

	void Manager::RestoreRecipe(const std::string& a_id, bool a_redo)
	{
		bool keysChanged = false;
		WithRecipeRetired(a_id, [&] {
			auto*      recipe = MutableRecipe(a_id);
			const auto history = histories_.find(a_id);
			if (!recipe || history == histories_.end()) {
				return;
			}
			auto restored = a_redo ? history->second.Redo(*recipe) : history->second.Undo(*recipe);
			if (!restored) {
				return;
			}
			keysChanged = restored->keys != recipe->keys;
			*recipe = std::move(*restored);
			recipe->id = a_id;
			Revalidate(a_id);
		});
		if (keysChanged) {
			QueueLoadedActorRefreshes();
		}
	}

	void Manager::UndoRecipe(std::string a_id)
	{
		PostTask([this, id = std::move(a_id)] { RestoreRecipe(id, false); });
	}

	void Manager::RedoRecipe(std::string a_id)
	{
		PostTask([this, id = std::move(a_id)] { RestoreRecipe(id, true); });
	}

	void Manager::SaveRecipe(std::string a_id)
	{
		PostTask([this, id = std::move(a_id)] {
			WithRecipeRetired(id, [&] {
				const auto*  recipe = MutableRecipe(id);
				const Recipe before = recipe ? *recipe : Recipe{};
				const auto   saved = WornEnchantmentPBR::SaveRecipe(id);
				if (!saved) {
					logger::error("recipe {}: save failed ({})", id, saved.error());
					return;
				}
				if (recipe && !(*recipe == before)) {
					histories_[id].Push(before);
				}
			});
		});
	}

	void Manager::RevertRecipe(std::string a_id)
	{
		PostTask([this, id = std::move(a_id)] {
			bool keysChanged = false;
			WithRecipeRetired(id, [&] {
				auto* recipe = MutableRecipe(id);
				if (!recipe) {
					return;
				}
				Recipe before = *recipe;
				if (WornEnchantmentPBR::RevertRecipe(id)) {
					keysChanged = recipe->keys != before.keys;
					if (!(*recipe == before)) {
						histories_[id].Push(std::move(before));
					}
					logger::info("recipe {}: reverted to its file", id);
				}
			});
			if (keysChanged) {
				QueueLoadedActorRefreshes();
			}
		});
	}

	void Manager::ReloadRecipes()
	{
		PostTask([this] {
			WithListMoved([&] {
				histories_.clear();
				LoadRecipes();
				paintReturn_ = {};
				const auto loaded = LoadedRecipes();
				for (const auto& id : view_.RecipeIDs()) {
					if (std::ranges::find(loaded, id, &Recipe::id) == loaded.end()) {
						view_.ForgetRecipe(id);
					}
				}
			});
		});
	}

	void Manager::RetireEveryActor()
	{
		std::vector<RE::FormID> ids;
		for (const auto& [actorID, state] : applied_) {
			ids.push_back(actorID);
		}
		for (const auto actorID : ids) {
			Retire(actorID);
		}
	}

	void Manager::NewRecipe(std::string a_id, RecipeKey a_key, std::string a_geometry)
	{
		PostTask([this, id = std::move(a_id), key = std::move(a_key), geometry = std::move(a_geometry)] {
			WithListMoved([&] { [[maybe_unused]] const bool made = WornEnchantmentPBR::NewRecipe(id, std::move(key), geometry); });
		});
	}

	void Manager::RenameRecipe(std::string a_from, std::string a_to)
	{
		PostTask([this, from = std::move(a_from), to = std::move(a_to)] {
			WithListMoved([&] {
				if (!WornEnchantmentPBR::RenameRecipe(from, to)) {
					return;
				}
				if (auto node = histories_.extract(from)) {
					node.key() = to;
					node.mapped().Rename(to);
					histories_.insert(std::move(node));
				}
				view_.RenameRecipe(from, to);
				if (paintReturn_.recipeID == from) {
					paintReturn_.recipeID = to;
				}
			});
		});
	}

	void Manager::BeginPaint(std::string a_active, RecipeKey a_key, Surface a_surface)
	{
		PostTask([this, active = std::move(a_active), key = std::move(a_key), a_surface] {
			const auto loaded = LoadedRecipes();
			const auto it = std::ranges::find(loaded, active, &Recipe::id);
			if (it == loaded.end()) {
				logger::warn("paint: recipe {} is not loaded", active);
				return;
			}
			Recipe paint = Studio::PaintRecipe(*it, key, a_surface);
			WithListMoved([&] {
				if (IsTransient(Studio::kPaintRecipe)) {
					[[maybe_unused]] const bool dropped = DropTransientRecipe(Studio::kPaintRecipe);
				}
				if (!AddTransientRecipe(std::move(paint))) {
					if (view_.isolateRecipe == Studio::kPaintRecipe) {
						view_.isolateRecipe = paintReturn_.recipeID;
						view_.isolateOutput = paintReturn_.output;
						view_.isolateLayer = paintReturn_.layer;
						view_.isolatedBySolo = paintReturn_.bySolo;
						paintReturn_ = {};
					}
					return;
				}
				histories_.erase(std::string{ Studio::kPaintRecipe });
				logger::info("paint: previewing {} on the {} through the paint recipe, keyed by {}", active, SurfaceName(a_surface), key.ToString());
				if (view_.isolateRecipe != Studio::kPaintRecipe) {
					paintReturn_ = { view_.isolateRecipe, view_.isolateOutput, view_.isolateLayer, view_.isolatedBySolo };
				}
				view_.isolateRecipe = std::string{ Studio::kPaintRecipe };
				view_.isolateOutput = -1;
				view_.isolateLayer = -1;
				view_.isolatedBySolo = false;
			});
		});
	}

	void Manager::SetPaintSurface(Surface a_surface)
	{
		EditRecipe(std::string{ Studio::kPaintRecipe }, Studio::EditBatch{ Studio::PaintSurfaceEdits(a_surface) });
	}

	void Manager::KeepPaint(std::string a_active, std::string a_name)
	{
		PostTask([this, active = std::move(a_active), name = std::move(a_name)] {
			const auto loaded = LoadedRecipes();
			const auto paint = std::ranges::find(loaded, std::string{ Studio::kPaintRecipe }, &Recipe::id);
			const auto target = std::ranges::find(loaded, active, &Recipe::id);
			if (paint == loaded.end() || target == loaded.end()) {
				logger::warn("keep: the paint recipe or {} is not loaded", active);
				return;
			}
			const Studio::EditBatch edits{ Studio::KeepEdits(*paint, *target, name) };
			ApplyEdits(active, edits);
			logger::info("keep: region {} written into {} ({} edit(s))", name, active, edits.edits.size());
		});
		EndPaint();
	}

	void Manager::EndPaint()
	{
		PostTask([this] {
			WithListMoved([&] {
				if (view_.isolateRecipe == Studio::kPaintRecipe) {
					view_.isolateRecipe = paintReturn_.recipeID;
					view_.isolateOutput = paintReturn_.output;
					view_.isolateLayer = paintReturn_.layer;
					view_.isolatedBySolo = paintReturn_.bySolo;
					paintReturn_ = {};
				}
				view_.ForgetRecipe(Studio::kPaintRecipe);
				[[maybe_unused]] const bool dropped = DropTransientRecipe(Studio::kPaintRecipe);
				histories_.erase(std::string{ Studio::kPaintRecipe });
			});
		});
	}

	void Manager::FireAt(RE::FormID a_actorID, std::string a_event, std::string a_node, Vec3 a_offset, float a_random, float a_value)
	{
		PostTask([this, a_actorID, event = std::move(a_event), node = std::move(a_node), a_offset, a_random, a_value] {
			EventRecord record;
			record.id = event;
			record.payload.value = a_value;
			record.payload.node = node;
			auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_actorID);
			auto* root = actor ? actor->Get3D(false) : nullptr;
			if (!node.empty() && root) {
				if (auto* object = root->GetObjectByName(RE::BSFixedString{ node })) {
					const auto& at = object->world.translate;
					Vec3        position{ at.x + a_offset.x, at.y + a_offset.y, at.z + a_offset.z };
					if (a_random > 0.0f) {
						static std::mt19937 gen{ std::random_device{}() };
						std::uniform_real_distribution<float> spread{ -a_random, a_random };
						position.x += spread(gen);
						position.y += spread(gen);
						position.z += spread(gen);
					}
					record.payload.position = position;
				} else {
					logger::warn("fire {}: node '{}' is not on the actor", event, node);
				}
			}
			Fire(a_actorID, record);
		});
	}

	void Manager::RequestMesh(RE::FormID a_actorID, std::string a_geometry)
	{
		PostTask([this, a_actorID, name = std::move(a_geometry)] {
			const auto it = applied_.find(a_actorID);
			if (it == applied_.end()) {
				return;
			}
			for (auto& piece : it->second.pieces) {
				for (auto& bound : piece.geometries) {
					if (bound.name != name || bound.lost) {
						continue;
					}
					auto* compositor = Compositor::GetSingleton();
					if (const auto mesh = compositor->MeshOf(bound.geometry.get()); !mesh) {
						logger::warn("mesh '{}': {}", name, mesh.error());
					}
					if (const auto& material = compositor->AnalyseMaterial(bound.inputs.material); !material.sample) {
						logger::warn("material of '{}': {}", name, material.problem);
					}
				}
			}
		});
	}

	void Manager::Retire(RE::FormID a_actorID)
	{
		const auto it = applied_.find(a_actorID);
		if (it == applied_.end()) {
			return;
		}
		const auto recipes = it->second.instances.size();
		const auto now = NowMS();
		for (const auto& instance : it->second.instances) {
			if (instance.recipe) {
				carriedTimes_[{ a_actorID, instance.recipe->id }] = CarriedTime{ instance.lastTime, now };
			}
		}
		applied_.erase(it);
		TextureLab::GetSingleton()->InvalidatePreviews();
		UnwatchAnimationEvents(RE::TESForm::LookupByID<RE::Actor>(a_actorID));
		if (GetSettings().verboseLogging) {
			logger::info("actor {:08X}: retired {} recipe(s)", a_actorID, recipes);
		}
	}

	void Manager::OnFrame()
	{
		FireDueFinalizes();
		const auto now = NowMS();
		if (now - lastTickMS_ < GetSettings().TickIntervalMS()) {
			return;
		}
		lastTickMS_ = now;
		if (!applied_.empty()) {
			Tick(now);
		}
		PublishSnapshot(now);
	}

	void Manager::Fire(RE::FormID a_actorID, const EventRecord& a_event)
	{
		const auto it = applied_.find(a_actorID);
		if (it == applied_.end()) {
			return;
		}
		for (auto& instance : it->second.instances) {
			instance.signals->Fire(a_event, instance.lastTime);
		}
	}
	void Manager::Tick(std::uint32_t a_nowMS)
	{
		const auto& settings = GetSettings();
		auto*       compositor = Compositor::GetSingleton();
		compositor->BeginTick(a_nowMS);
		TextureLab::GetSingleton()->RenderPreviews();
		const bool resuming = frozenLastTick_ && !view_.freeze;
		frozenLastTick_ = view_.freeze;
		for (auto it = applied_.begin(); it != applied_.end();) {
			auto& state = it->second;
			DropLostGeometries(state);
			for (auto& instance : state.instances) {
				const float speed = settings.animationSpeed * view_.speed * instance.recipe->clock.speed;
				if (resuming && speed > 0.0f) {
					instance.startMS = a_nowMS - static_cast<std::uint32_t>(view_.scrubSeconds / speed * 1000.0f);
				}
				const float time = view_.freeze ? view_.scrubSeconds : static_cast<float>(a_nowMS - instance.startMS) * 0.001f * speed;
				const float delta = std::max(0.0f, time - instance.lastTime);
				TickInstance(instance, time, delta);
				instance.lastTime = time;
			}
			for (auto& piece : state.pieces) {
				for (auto& bound : piece.geometries) {
					RenderGeometry(state, bound);
				}
			}
			UpdateLights(state);
			it = Alive(state) ? std::next(it) : applied_.erase(it);
		}
		if (compositor->MeshSweepDue(a_nowMS)) {
			std::vector<RE::BSGeometry*> bound;
			for (const auto& [actorID, state] : applied_) {
				for (const auto& piece : state.pieces) {
					for (const auto& g : piece.geometries) {
						if (!g.lost) {
							bound.push_back(g.geometry.get());
						}
					}
				}
			}
			compositor->SweepMeshes(a_nowMS, bound);
		}
	}

	void Manager::TickInstance(RecipeInstance& a_instance, float a_time, float a_delta)
	{
		if (view_.freeze && a_time + 0.001f < a_instance.lastTime) {
			a_instance.signals = std::make_unique<SignalState>(*a_instance.graph);
			a_delta = a_time;
		}
		a_instance.signals->Tick(*a_instance.environment, { a_time, a_delta });
	}

	void Manager::DropLostGeometries(ActorState& a_state)
	{
		for (auto& piece : a_state.pieces) {
			for (auto& bound : piece.geometries) {
				if (bound.lost) {
					continue;
				}
				if ((bound.material && !bound.material->StillOwned()) || (bound.shell && !bound.shell->StillOwned())) {
					logger::info("dropping '{}': its material or shell was replaced by another system", bound.name);
					bound.lost = true;
					bound.material.reset();
					bound.shell.reset();
					bound.shellOwner.reset();
					bound.plan = GeometryPlan{};
				}
			}
		}
	}

	void Manager::RenderGeometry(ActorState& a_state, GeometryBinding& a_bound)
	{
		if (a_bound.lost) {
			return;
		}
		const bool anyLayerHidden = view_.isolateLayer >= 0 || !view_.muted.empty();
		for (const auto& slot : a_bound.plan.slots) {
			auto* target = TargetFor(a_bound, slot.surface);
			if (!target) {
				continue;
			}
			SlotWrite                         write = EmptyWrite(slot.slot);
			StackBase                         base;
			std::vector<const SurfaceOutput*> shown;
			std::vector<const SignalState*>   signals;
			for (const auto& c : slot.chain) {
				auto&       placement = a_state.placements[a_bound.placements[c.placed]];
				const auto& instance = a_state.instances[placement.instance];
				const auto* material = c.output < instance.recipe->outputs.size() ? Get<SurfaceOutput>(instance.recipe->outputs[c.output]) : nullptr;
				auto*       output = OutputAt(placement, c.output);
				if (!material || !output || !output->stack || !view_.OutputShown(instance.recipe->id, c.output)) {
					continue;
				}
				LayerFilter filter;
				if (anyLayerHidden) {
					filter = HiddenLayers(view_, instance.recipe->id, c.output, material->stack.size());
				}
				Compositor::GetSingleton()->Render(*output->stack, *instance.signals, instance.lastTime, filter, base);
				write.shown = true;
				if (auto* texture = output->stack->Texture()) {
					base = StackBase{ texture, base.animated || output->stack->Animated() };
					write.texture = texture;
				}
				shown.push_back(material);
				signals.push_back(instance.signals.get());
			}
			for (const auto field : ScalarsOf(slot.slot)) {
				if (const auto at = ScalarSource(slot.slot, field, shown)) {
					TakeScalar(write, field, shown[*at]->scalars, *signals[*at]);
				}
			}
			WriteSlot(*target, write);
		}
		if (a_bound.shell && a_bound.shellOwner) {
			const auto& instance = a_state.instances[*a_bound.shellOwner];
			const auto& shell = instance.recipe->shell;
			auto&       signals = *instance.signals;
			a_bound.shell->Pose(signals.Resolve(shell.pose.inflate), signals.Resolve(shell.alpha), signals.Resolve(shell.rimPower), signals.Resolve(shell.emissive));
			a_bound.shell->SetVisible(view_.RecipeShown(instance.recipe->id));
		}
	}

	void Manager::UpdateLights(ActorState& a_state)
	{
		for (auto& instance : a_state.instances) {
			if (!instance.light || !instance.lightOutput) {
				continue;
			}
			const auto* light = *instance.lightOutput < instance.recipe->outputs.size() ? Get<LightOutput>(instance.recipe->outputs[*instance.lightOutput]) : nullptr;
			if (!light) {
				continue;
			}
			auto& signals = *instance.signals;
			instance.light->Update(signals.Resolve(light->color), signals.Resolve(light->intensity), signals.Resolve(light->size), signals.Resolve(light->cutoff),
				view_.OutputShown(instance.recipe->id, *instance.lightOutput));
		}
	}

	bool Manager::Alive(const ActorState& a_state) noexcept
	{
		for (const auto& piece : a_state.pieces) {
			for (const auto& bound : piece.geometries) {
				if (!bound.lost) {
					return true;
				}
			}
		}
		return std::ranges::any_of(a_state.instances, [](const RecipeInstance& a_instance) { return static_cast<bool>(a_instance.light); });
	}

	Manager::Status Manager::GetStatus() const
	{
		Status s;
		s.emissivePath = emissivePathEnabled_;
		s.layoutVerified = layoutVerified_;
		s.runtimeLab = TextureLab::GetSingleton()->Available();
		s.actors = static_cast<std::uint32_t>(applied_.size());
		for (const auto& [id, state] : applied_) {
			s.pieces += static_cast<std::uint32_t>(state.pieces.size());
			s.recipes += static_cast<std::uint32_t>(state.instances.size());
			for (const auto& piece : state.pieces) {
				for (const auto& bound : piece.geometries) {
					if (bound.lost) {
						continue;
					}
					++s.geometries;
					s.shells += bound.shell ? 1 : 0;
				}
			}
			for (const auto& instance : state.instances) {
				s.lights += instance.light ? 1 : 0;
			}
		}
		s.tickMS = GetSettings().TickIntervalMS();
		return s;
	}

	void Manager::Watch(const std::optional<Studio::PieceRef>& a_request)
	{
		std::scoped_lock lock{ snapshotLock_ };
		watch_ = a_request;
		watchedMS_ = NowMS();
	}

	std::shared_ptr<const Manager::Snapshot> Manager::LatestSnapshot() const
	{
		std::scoped_lock lock{ snapshotLock_ };
		return latest_;
	}

	void Manager::PublishSnapshot(std::uint32_t a_nowMS)
	{
		std::optional<Studio::PieceRef> request;
		{
			std::scoped_lock lock{ snapshotLock_ };
			if (watchedMS_ == 0 || a_nowMS > watchedMS_ + kWatchWindowMS) {
				return;
			}
			request = watch_;
		}
		auto built = std::make_shared<Snapshot>(BuildSnapshot(request));
		built->version = ++snapshotVersion_;
		built->tickMS = a_nowMS;
		built->view = view_;
		std::scoped_lock lock{ snapshotLock_ };
		latest_ = std::move(built);
	}

	Manager::Snapshot Manager::BuildSnapshot(const std::optional<Studio::PieceRef>& a_request) const
	{
		Snapshot   out;
		const auto refOf = [](RE::FormID a_actorID, const AppliedPiece& a_piece) {
			return Studio::PieceRef{ a_actorID, a_piece.armor, a_piece.firstPerson };
		};
		const auto matches = [&](RE::FormID a_actorID, const AppliedPiece& a_piece) {
			return a_request && *a_request == refOf(a_actorID, a_piece);
		};
		bool anyMatch = false;
		for (const auto& [actorID, state] : applied_) {
			for (const auto& piece : state.pieces) {
				anyMatch = anyMatch || matches(actorID, piece);
			}
		}
		bool first = true;
		for (const auto& [actorID, state] : applied_) {
			const auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorID);
			for (const auto& piece : state.pieces) {
				const bool full = anyMatch ? matches(actorID, piece) : first;
				first = false;
				Snapshot::PieceRow row;
				row.ref = refOf(actorID, piece);
				row.actorName = actor && actor->GetName() ? actor->GetName() : "?";
				row.armorName = piece.armorName;
				for (const auto& source : KeyChoicesOf(piece.piece)) {
					Studio::KeyChoice key;
					key.key = source;
					const auto* form = LookupForm(source.form);
					const auto  editorID = form ? EditorIdOf(*form) : std::string{};
					key.text = editorID.empty() ? source.form.ToString() : editorID;
					row.keys.push_back(std::move(key));
				}
				for (std::size_t m = 0; m < piece.matches.size(); ++m) {
					const auto& match = piece.matches[m];
					const auto& instance = state.instances[match.instance];
					const auto& recipe = *instance.recipe;
					Snapshot::RecipeRow r;
					r.id = recipe.id;
					r.key = match.key.ToString();
					r.keys = recipe.keys;
					r.priority = match.priority;
					r.time = instance.lastTime;
					r.dirty = IsDirty(r.id);
					r.pinned = view_.pin && view_.pin->piece == row.ref && view_.pin->recipeID == r.id;
					r.shellMaterial = recipe.shell.material;
					r.lightOutput = instance.lightOutput;
					r.lightRow = Studio::LightRowOf(recipe);
					r.shellRow = Studio::ShellRowOf(recipe);
					if (const auto history = histories_.find(r.id); history != histories_.end()) {
						r.undoDepth = history->second.UndoDepth();
						r.redoDepth = history->second.RedoDepth();
					}
					if (!full) {
						row.recipes.push_back(std::move(r));
						continue;
					}
					for (const auto& mask : recipe.masks) {
						r.masks.push_back(mask.name);
					}
					const auto* counted = ReferencesOf(r.id);
					const auto  references = counted ? *counted : Studio::CountReferences(recipe);
					for (const auto& source : recipe.sources) {
						const auto count = references.images.find(source.name);
						r.sourceRows.push_back(Studio::SourceRowOf(source, count != references.images.end() ? count->second : 0));
					}
					for (const auto& mask : recipe.masks) {
						const auto count = references.images.find(mask.name);
						r.maskRows.push_back({ mask.name, mask.text, count != references.images.end() ? count->second : 0 });
					}
					if (const auto origin = OriginOf(recipe)) {
						r.problems.assign(origin->diagnostics.begin(), origin->diagnostics.end());
					}
					const auto inertReasons = InertReasons(*instance.graph);
					for (std::size_t i = 0; i < instance.graph->Size(); ++i) {
						const auto& signal = instance.graph->At(i);
						Snapshot::SignalRow srow;
						srow.name = signal.name;
						srow.kind = SignalKindOf(signal.kind);
						srow.type = instance.graph->TypeOf(i);
						srow.value = instance.signals->ValueOf(i);
						srow.inert = instance.graph->Inert(i);
						if (srow.inert) {
							if (const auto reason = inertReasons.find(signal.name); reason != inertReasons.end()) {
								srow.problem = reason->second;
							}
						}
						if (const auto* declared = recipe.FindSignal(signal.name)) {
							srow.definition = declared->kind;
							if (const auto* constant = Get<ConstantSignal>(declared->kind)) {
								srow.constant = constant->value;
							}
							if (const auto* expr = Get<ExprSignal>(declared->kind)) {
								srow.text = expr->text;
							}
							if (const auto* trigger = Get<TriggerSignal>(declared->kind)) {
								if (const auto* event = Get<EventOrigin>(trigger->origin)) {
									srow.event = event->event;
								} else if (const auto* plugin = Get<PluginOrigin>(trigger->origin)) {
									srow.event = plugin->id;
								}
							}
							srow.curve = declared->curve ? declared->curve->text : "";
						}
						if (const auto count = references.signals.find(srow.name); count != references.signals.end()) {
							srow.references = count->second;
						}
						r.signals.push_back(std::move(srow));
					}
					for (const auto& curve : recipe.curves) {
						const auto count = references.curves.find(curve.name);
						r.curves.push_back({ curve.name, curve.text, count != references.curves.end() ? count->second : 0 });
					}
					for (const auto& bound : piece.geometries) {
						if (bound.lost || m >= bound.placements.size()) {
							continue;
						}
						const auto& placement = state.placements[bound.placements[m]];
						Snapshot::GeometryRow gr;
						gr.name = bound.name;
						gr.privateMaterial = bound.material && bound.material->Private();
						gr.shell = bound.shell ? bound.shell->Describe() : "";
						auto* compositor = Compositor::GetSingleton();
						if (const auto entry = compositor->CachedMesh(bound.geometry.get()); entry && entry->mesh) {
							gr.meshRead = true;
							gr.partitions = entry->facts.partitions;
							gr.bones = entry->facts.bones;
							gr.islands = entry->analysis.islands;
						}
						if (const auto* material = compositor->CachedMaterial(bound.inputs.material); material && material->analysis) {
							gr.clusters = material->analysis->clusters;
						}
						gr.materialSlots = bound.material ? SlotRows(*bound.material) : std::vector<Snapshot::SlotRow>{};
						gr.shellSlots = bound.shell ? SlotRows(*bound.shell) : std::vector<Snapshot::SlotRow>{};
						for (const auto& source : recipe.sources) {
							Snapshot::PictureRow prow;
							prow.name = source.name;
							prow.description = DescribeSource(source.kind);
							prow.type = SourceType(source);
							if (const auto prepared = compositor->InspectSource(recipe, source.name, bound.inputs)) {
								prow.texture = prepared->texture.get();
								prow.channel = prepared->sampling.channel;
								prow.animated = prepared->animated;
								prow.problem = prepared->problem;
							}
							gr.sources.push_back(std::move(prow));
						}
						for (const auto& mask : recipe.masks) {
							Snapshot::PictureRow prow;
							prow.name = mask.name;
							prow.description = mask.text;
							if (const auto prepared = compositor->InspectMask(recipe, mask.name, bound.inputs)) {
								prow.texture = prepared->texture.get();
								prow.channel = prepared->channel;
								prow.animated = prepared->animated;
								prow.problem = prepared->problem;
							}
							gr.masks.push_back(std::move(prow));
						}
						for (const auto& o : placement.outputs) {
							const auto* material = o.index < recipe.outputs.size() ? Get<SurfaceOutput>(recipe.outputs[o.index]) : nullptr;
							Snapshot::OutputRow orow;
							orow.index = o.index;
							orow.target = material ? TargetOf(material->surface) : Target::kLight;
							if (material) {
								orow.surface = material->surface;
								orow.slot = material->slot;
								orow.replace = material->replace;
							}
							if (const auto merge = MergeOf(bound.plan, Contribution{ m, o.index })) {
								orow.merged = true;
								orow.merge = *merge;
							}
							orow.animated = o.stack && o.stack->Animated();
							orow.size = o.stack ? o.stack->Size().Pixels() : 0;
							orow.problem = o.problem;
							orow.texture = o.stack ? o.stack->Texture() : nullptr;
							if (material) {
								const auto& sc = material->scalars;
								const auto& sig = *instance.signals;
								for (const auto field : ScalarsOf(material->slot)) {
									const std::string name{ ScalarFieldName(field) };
									if (field == ScalarField::kColor) {
										if (sc.color) {
											orow.scalars.push_back({ name, sig.Resolve(*sc.color), Vec3ParamText(*sc.color) });
										}
									} else if (const auto* param = ScalarOf(sc, field); param && *param) {
										orow.scalars.push_back({ name, sig.Resolve(**param), ParamText(**param) });
									}
								}
								for (const auto& layer : material->stack) {
									Snapshot::LayerRow lrow;
									lrow.source = LayerSourceText(layer.source);
									lrow.mask = layer.mask ? "@" + layer.mask->name : "";
									lrow.blend = std::string{ BlendName(layer.blend) };
									lrow.opacity = sig.Resolve(layer.opacity);
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
					r.light = instance.light ? instance.light->Describe() : "";
					row.recipes.push_back(std::move(r));
				}
				out.pieces.push_back(std::move(row));
			}
		}
		for (const auto& recipe : LoadedRecipes()) {
			if (!IsTransient(recipe.id)) {
				out.loaded.push_back(recipe.id);
			}
		}
		return out;
	}
}
