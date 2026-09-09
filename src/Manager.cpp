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
		SlotTarget* TargetFor(BoundGeometry& a_bound, Surface a_surface)
		{
			if (a_surface == Surface::kShell) {
				return a_bound.shell.get();
			}
			return a_bound.material.get();
		}

		struct TickScalars
		{
			const SignalState& signals;
			const SlotScalars& scalars;
			bool               shown;

			[[nodiscard]] float Of(ScalarField a_field) const
			{
				const auto* param = ScalarOf(scalars, a_field);
				return param && *param ? signals.Resolve(**param) : ScalarFallback(a_field);
			}
			[[nodiscard]] float Shown(ScalarField a_field) const { return shown ? Of(a_field) : 0.0f; }
			[[nodiscard]] Vec3  Color() const
			{
				const float fallback = ScalarFallback(ScalarField::kColor);
				return scalars.color ? signals.Resolve(*scalars.color) : Vec3{ fallback, fallback, fallback };
			}
		};

		void WriteSlot(SlotTarget& a_target, const SurfaceOutput& a_output, const SignalState& a_signals, RE::NiSourceTexture* a_texture, bool a_shown)
		{
			const TickScalars tick{ a_signals, a_output.scalars, a_shown };
			a_target.WriteTexture(a_output.slot, a_shown ? a_texture : nullptr);
			switch (a_output.slot) {
			case Slot::kEmissive:
				a_target.WriteEmissive(Vec3{ 1.0f, 1.0f, 1.0f }, tick.Shown(ScalarField::kStrength));
				break;
			case Slot::kFuzz:
				a_target.WriteFuzz(tick.Color(), tick.Shown(ScalarField::kWeight));
				break;
			case Slot::kHeight:
				a_target.WriteHeightScale(tick.Shown(ScalarField::kScale));
				break;
			case Slot::kGlint:
				a_target.WriteGlint(tick.Of(ScalarField::kScreenSpaceScale), tick.Of(ScalarField::kLogMicrofacetDensity), tick.Of(ScalarField::kMicrofacetRoughness), tick.Of(ScalarField::kDensityRandomization), a_shown);
				break;
			case Slot::kCoat:
				a_target.WriteCoat(tick.Of(ScalarField::kRoughness), tick.Shown(ScalarField::kLevel));
				break;
			case Slot::kSubsurface:
				a_target.WriteSubsurface(tick.Color(), tick.Shown(ScalarField::kThickness));
				break;
			default:
				break;
			}
		}

		Replaced ReplacedBy(std::span<const AppliedRecipe> a_later)
		{
			Replaced replaced;
			for (const auto& later : a_later) {
				for (const auto& output : later.recipe->outputs) {
					if (const auto* material = Get<SurfaceOutput>(output); material && material->replace) {
						replaced.slots.emplace(material->slot, later.recipe->id);
					} else if (const auto* light = Get<LightOutput>(output); light && light->replace) {
						replaced.light = later.recipe->id;
					}
				}
			}
			return replaced;
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
			const auto* material = Get<SurfaceOutput>(recipe.outputs[i]);
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
					auto* target = TargetFor(bound, material->surface);
					output.problem = target ? target->Problem(material->slot) : "the surface is not bound";
				}
				if (output.problem.empty()) {
					output.stack = Compositor::GetSingleton()->Prepare(recipe, *material, inputs, TextureSize::Clamp(settings.runtimeTextureSize), TextureSize::Clamp(settings.glossMapSize));
					if (!output.stack) {
						output.problem = "the texture lab is unavailable";
					} else {
						for (const auto& d : output.stack->Diagnostics()) {
							logger::warn("recipe {} output {} on '{}': {}: {}", recipe.id, i, bound.name, d.where, d.message);
						}
					}
				}
			}
			outputsLog += std::format("{}{}->{}{}", outputsLog.empty() ? "" : ", ", SlotName(material->slot), SurfaceName(material->surface),
				output.problem.empty() ? (output.stack && output.stack->Animated() ? " (animated)" : " (static)") : std::format(" [{}]", output.problem));
			bound.outputs.push_back(std::move(output));
		}
		if (settings.verboseLogging) {
			logger::info("apply recipe {} armor actor {:08X} geometry '{}' material={} {} outputs: {}", recipe.id, a_actor->GetFormID(), bound.name,
				bound.material ? (bound.material->Private() ? "private" : "shared") : "untouched", bound.shell ? bound.shell->Describe() : "no shell", outputsLog);
		}
		a_applied.geometries.push_back(std::move(bound));
		return true;
	}

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
				for (auto& applied : piece.recipes) {
					for (auto& bound : applied.geometries) {
						if (bound.name == name) {
							auto* compositor = Compositor::GetSingleton();
							if (const auto mesh = compositor->MeshOf(bound.geometry.get()); !mesh) {
								logger::warn("mesh '{}': {}", name, mesh.error());
							}
							if (const auto& material = compositor->AnalyseMaterial(bound.inputs.material); !material.sample) {
								logger::warn("material of '{}': {}", name, material.problem);
							}
						}
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
		for (auto& piece : it->second.pieces) {
			for (auto& applied : piece.recipes) {
				applied.signals->Fire(a_event, applied.lastTime);
			}
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
		if (compositor->MeshSweepDue(a_nowMS)) {
			std::vector<RE::BSGeometry*> bound;
			for (const auto& [actorID, state] : applied_) {
				for (const auto& piece : state.pieces) {
					for (const auto& applied : piece.recipes) {
						for (const auto& g : applied.geometries) {
							bound.push_back(g.geometry.get());
						}
					}
				}
			}
			compositor->SweepMeshes(a_nowMS, bound);
		}
	}

	void Manager::TickRecipe(AppliedRecipe& a_applied, float a_time, float a_delta)
	{
		const auto& recipe = *a_applied.recipe;
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
				const auto* material = output.index < recipe.outputs.size() ? Get<SurfaceOutput>(recipe.outputs[output.index]) : nullptr;
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
			const auto* light = *a_applied.lightOutput < recipe.outputs.size() ? Get<LightOutput>(recipe.outputs[*a_applied.lightOutput]) : nullptr;
			if (light) {
				a_applied.light->Update(signals.Resolve(light->color), signals.Resolve(light->intensity), signals.Resolve(light->size), signals.Resolve(light->cutoff), view.OutputShown(recipe.id, *a_applied.lightOutput));
			}
		}
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
		Snapshot out;
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
				for (const auto& applied : piece.recipes) {
					Snapshot::RecipeRow r;
					r.id = applied.recipe->id;
					r.key = applied.key.ToString();
					r.keys = applied.recipe->keys;
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
					if (!full) {
						row.recipes.push_back(std::move(r));
						continue;
					}
					for (const auto& mask : applied.recipe->masks) {
						r.masks.push_back(mask.name);
					}
					const auto* counted = ReferencesOf(r.id);
					const auto  references = counted ? *counted : Studio::CountReferences(*applied.recipe);
					for (const auto& source : applied.recipe->sources) {
						const auto count = references.images.find(source.name);
						r.sourceRows.push_back(Studio::SourceRowOf(source, count != references.images.end() ? count->second : 0));
					}
					for (const auto& mask : applied.recipe->masks) {
						const auto count = references.images.find(mask.name);
						r.maskRows.push_back({ mask.name, mask.text, count != references.images.end() ? count->second : 0 });
					}
					if (const auto origin = OriginOf(*applied.recipe)) {
						r.problems.assign(origin->diagnostics.begin(), origin->diagnostics.end());
					}
					const auto inertReasons = InertReasons(*applied.graph);
					for (std::size_t i = 0; i < applied.graph->Size(); ++i) {
						const auto& signal = applied.graph->At(i);
						Snapshot::SignalRow row;
						row.name = signal.name;
						row.kind = SignalKindOf(signal.kind);
						row.type = applied.graph->TypeOf(i);
						row.value = applied.signals->ValueOf(i);
						row.inert = applied.graph->Inert(i);
						if (row.inert) {
							if (const auto reason = inertReasons.find(signal.name); reason != inertReasons.end()) {
								row.problem = reason->second;
							}
						}
						if (const auto* declared = applied.recipe->FindSignal(signal.name)) {
							row.definition = declared->kind;
							if (const auto* constant = Get<ConstantSignal>(declared->kind)) {
								row.constant = constant->value;
							}
							if (const auto* expr = Get<ExprSignal>(declared->kind)) {
								row.text = expr->text;
							}
							if (const auto* trigger = Get<TriggerSignal>(declared->kind)) {
								if (const auto* event = Get<EventOrigin>(trigger->origin)) {
									row.event = event->event;
								} else if (const auto* plugin = Get<PluginOrigin>(trigger->origin)) {
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
						auto* compositor = Compositor::GetSingleton();
						if (const auto entry = compositor->CachedMesh(g.geometry.get()); entry && entry->mesh) {
							gr.meshRead = true;
							gr.partitions = entry->facts.partitions;
							gr.bones = entry->facts.bones;
							gr.islands = entry->analysis.islands;
						}
						if (const auto* material = compositor->CachedMaterial(g.inputs.material); material && material->analysis) {
							gr.clusters = material->analysis->clusters;
						}
						gr.materialSlots = g.material ? SlotRows(*g.material) : std::vector<Snapshot::SlotRow>{};
						gr.shellSlots = g.shell ? SlotRows(*g.shell) : std::vector<Snapshot::SlotRow>{};
						for (const auto& source : applied.recipe->sources) {
							Snapshot::PictureRow row;
							row.name = source.name;
							row.description = DescribeSource(source.kind);
							row.type = SourceType(source);
							if (const auto prepared = compositor->InspectSource(*applied.recipe, source.name, g.inputs)) {
								row.texture = prepared->texture.get();
								row.channel = prepared->sampling.channel;
								row.animated = prepared->animated;
								row.problem = prepared->problem;
							}
							gr.sources.push_back(std::move(row));
						}
						for (const auto& mask : applied.recipe->masks) {
							Snapshot::PictureRow row;
							row.name = mask.name;
							row.description = mask.text;
							if (const auto prepared = compositor->InspectMask(*applied.recipe, mask.name, g.inputs)) {
								row.texture = prepared->texture.get();
								row.channel = prepared->channel;
								row.animated = prepared->animated;
								row.problem = prepared->problem;
							}
							gr.masks.push_back(std::move(row));
						}
						for (const auto& o : g.outputs) {
							const auto* material = o.index < applied.recipe->outputs.size() ? Get<SurfaceOutput>(applied.recipe->outputs[o.index]) : nullptr;
							Snapshot::OutputRow orow;
							orow.index = o.index;
							orow.target = material ? TargetOf(material->surface) : Target::kLight;
							if (material) {
								orow.surface = material->surface;
								orow.slot = material->slot;
								orow.replace = material->replace;
							}
							orow.animated = o.stack && o.stack->Animated();
							orow.size = o.stack ? o.stack->Size().Pixels() : 0;
							orow.problem = o.problem;
							orow.texture = o.stack ? o.stack->Texture() : nullptr;
							if (material) {
								const auto& sc = material->scalars;
								const auto& sig = *applied.signals;
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
