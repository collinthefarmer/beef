#include "Menu.h"

#include "ComposePage.h"
#include "Identity.h"
#include "Manager.h"
#include "MenuState.h"
#include "MenuWidgets.h"
#include "RecipeStore.h"
#include "Settings.h"
#include "Studio.h"

#include <algorithm>
#include <format>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#include "extern/SKSEMenuFramework.h"
#pragma clang diagnostic pop

// The SDK header keeps the ImGui wrappers and types in ImGuiMCP.
namespace ImGui = ImGuiMCP;
using ImGuiMCP::ImVec2;
using ImGuiMCP::ImGuiSliderFlags_Logarithmic;

// Registration, the header every page shares, and the pages beside the
// studio: Recipes, Setup and Log. Pages read the manager's snapshot, taken
// once per frame, never the live state.
namespace WornEnchantmentPBR
{
	namespace
	{
		namespace Widgets = Studio::Widgets;

		bool     g_needsReapply = false;
		bool     g_autoReapply = true;
		Settings g_savedSettings{};
		bool     g_savedKnown = false;

		constexpr int kTableFlags = ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_Borders | ImGuiMCP::ImGuiTableFlags_SizingStretchProp;

		// ----------------------------------------------------------- settings

		void MarkReapply(bool a_changed)
		{
			if (a_changed) {
				g_needsReapply = true;
				if (g_autoReapply) {
					Manager::GetSingleton()->ReapplyAll();
					g_needsReapply = false;
				}
			}
		}

		// One widget per settings-table row.
		void Widget(Settings& s, const SettingDesc& d)
		{
			using W = SettingDesc::Widget;
			bool changed = false;
			std::visit([&](auto a_member) {
				using T = std::remove_cvref_t<decltype(s.*a_member)>;
				if constexpr (std::is_same_v<T, bool>) {
					changed = ImGui::Checkbox(d.label, &(s.*a_member));
				} else if constexpr (std::is_same_v<T, float>) {
					changed = ImGui::SliderFloat(d.label, &(s.*a_member), d.min, d.max, d.max - d.min > 10 ? "%.2f" : "%.3f", d.widget == W::kLogSlider ? ImGuiSliderFlags_Logarithmic : 0);
				} else {
					if (d.widget == W::kEnum && d.items) {
						int v = static_cast<int>(s.*a_member);
						if (ImGui::Combo(d.label, &v, d.items)) {
							s.*a_member = static_cast<std::uint32_t>(v);
							changed = true;
						}
					} else if (d.widget == W::kSizeCombo) {
						static const char* sizes[] = { "64", "128", "256", "512", "1024", "2048", "4096" };
						int                lo = 0;
						while ((64u << lo) < static_cast<std::uint32_t>(d.min)) {
							++lo;
						}
						int hi = lo;
						while ((64u << hi) < static_cast<std::uint32_t>(d.max)) {
							++hi;
						}
						int idx = lo;
						while (idx < hi && (64u << idx) < s.*a_member) {
							++idx;
						}
						int rel = idx - lo;
						if (ImGui::Combo(d.label, &rel, sizes + lo, hi - lo + 1)) {
							s.*a_member = 64u << (lo + rel);
							changed = true;
						}
					} else {
						int v = static_cast<int>(s.*a_member);
						if (ImGui::SliderInt(d.label, &v, static_cast<int>(d.min), static_cast<int>(d.max))) {
							s.*a_member = static_cast<std::uint32_t>(v);
							changed = true;
						}
					}
				}
			},
				d.member);
			Widgets::HelpMarker(d.help);
			if (d.reapply) {
				MarkReapply(changed);
			}
		}

		void Group(Settings& s, const char* a_page)
		{
			for (const auto& d : SettingTable()) {
				if (std::string_view{ d.page } == a_page) {
					Widget(s, d);
				}
			}
		}

		void SaveBar(Settings& s)
		{
			auto* manager = Manager::GetSingleton();
			ImGui::Separator();
			if (!g_savedKnown) {
				g_savedSettings = s;
				g_savedKnown = true;
			}
			if (ImGui::Button("Save INI")) {
				if (SaveSettingsToDisk(s)) {
					g_savedSettings = s;
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Reload INI")) {
				SetSettings(LoadSettingsFromDisk());
				g_savedSettings = GetSettings();
				g_needsReapply = false;
				manager->ReapplyAll();
			}
			ImGui::SameLine();
			if (ImGui::Button("Re-apply")) {
				g_needsReapply = false;
				manager->ReapplyAll();
			}
			ImGui::SameLine();
			ImGui::Checkbox("auto", &g_autoReapply);
			Widgets::HelpMarker("Re-apply automatically when a setting that is read at apply time changes.");
			if (SettingsDiffer(s, g_savedSettings)) {
				ImGui::SameLine();
				Widgets::Warn("unsaved changes");
			}
			if (g_needsReapply) {
				ImGui::SameLine();
				Widgets::Warn("re-apply needed");
			}
		}

		// -------------------------------------------------------------- pages

		void __stdcall RenderRecipes()
		{
			auto*      manager = Manager::GetSingleton();
			const auto snapshot = manager->TakeSnapshot();
			RenderHeader(snapshot);
			const auto& selection = Studio::State().selection;

			if (ImGui::Button("Reload recipes")) {
				manager->ReloadRecipes();
			}
			ImGui::SameLine();
			if (ImGui::Button("Re-apply all")) {
				manager->ReapplyAll();
			}
			ImGui::SameLine();
			if (ImGui::Button("Retire all (baseline)")) {
				manager->RetireAll();
			}

			ImGui::SeparatorText("Loaded");
			if (ImGui::BeginTable("recipes", 5, kTableFlags)) {
				ImGui::TableSetupColumn("recipe");
				ImGui::TableSetupColumn("keys");
				ImGui::TableSetupColumn("rows");
				ImGui::TableSetupColumn("state");
				ImGui::TableSetupColumn("file");
				ImGui::TableHeadersRow();
				for (const auto& recipe : LoadedRecipes()) {
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(recipe.id.c_str());
					ImGui::TableNextColumn();
					std::string keys;
					for (const auto& k : recipe.keys) {
						keys += (keys.empty() ? "" : ", ") + k.ToString();
					}
					ImGui::TextWrapped("%s", keys.c_str());
					ImGui::TableNextColumn();
					ImGui::Text("%zu signals, %zu curves, %zu sources, %zu masks, %zu outputs", recipe.signals.size(), recipe.curves.size(), recipe.sources.size(), recipe.masks.size(), recipe.outputs.size());
					ImGui::TableNextColumn();
					const auto  origin = OriginOf(recipe);
					std::size_t errors = 0, warnings = 0;
					if (origin) {
						for (const auto& d : origin->diagnostics) {
							(d.severity == Severity::kError ? errors : warnings)++;
						}
					}
					if (errors) {
						Widgets::Problem(std::format("{} error(s)", errors));
					} else if (warnings) {
						Widgets::Warn(std::format("{} warning(s)", warnings));
					} else {
						Widgets::Ok("ok");
					}
					if (!recipe.metadata.imported.empty()) {
						ImGui::SameLine();
						Widgets::Dim("imported, not yet edited");
					}
					ImGui::TableNextColumn();
					ImGui::TextWrapped("%s", origin ? origin->path.string().c_str() : "");
				}
				ImGui::EndTable();
			}

			const auto* piece = Studio::SelectedPiece(snapshot, selection);
			ImGui::SeparatorText("Resolved for the selection (merge order)");
			if (!piece) {
				ImGui::TextDisabled("nothing applied; equip enchanted PBR armor or press Re-apply all");
				return;
			}
			for (const auto& r : piece->recipes) {
				ImGui::BulletText("%s  by %s  priority %d  t %.1fs  %zu geometr%s%s%s", r.id.c_str(), r.key.c_str(), r.priority, r.time, r.geometries.size(), r.geometries.size() == 1 ? "y" : "ies",
					r.light.empty() ? "" : "  ", r.light.c_str());
				for (const auto& g : r.geometries) {
					std::string outputs;
					for (const auto& o : g.outputs) {
						outputs += std::format("{}{}->{}{}", outputs.empty() ? "" : ", ", o.slotName, o.target, o.problem.empty() ? (o.animated ? " (animated)" : " (static)") : std::format(" [{}]", o.problem));
					}
					ImGui::Indent();
					ImGui::TextWrapped("%s  [%s]%s%s  %s", Studio::GeometryLabel(g.name, piece->armorName).c_str(), g.privateMaterial ? "private material" : "material untouched", g.shell.empty() ? "" : "  ", g.shell.c_str(), outputs.c_str());
					ImGui::Unindent();
				}
			}
			ImGui::SeparatorText("Board: what the selected recipe writes");
			Studio::DrawBoardPage(snapshot);
			const auto* selected = Studio::SelectedRecipe(piece, selection);
			if (!selected) {
				return;
			}
			ImGui::SeparatorText(selected->dirty ? std::format("{} (edited, not saved)", selected->id).c_str() : selected->id.c_str());
			if (ImGui::Button("Save")) {
				manager->SaveRecipe(selected->id);
			}
			ImGui::SameLine();
			if (ImGui::Button("Revert to file")) {
				manager->RevertRecipe(selected->id);
			}
			ImGui::SameLine();
			Widgets::HelpMarker("Save writes the recipe to its file. An imported recipe is saved to user/<id>.json with its imported line dropped, and loads from there afterwards.");
			if (!selected->problems.empty()) {
				ImGui::SeparatorText("Rows with problems");
				for (const auto& d : selected->problems) {
					const auto line = std::format("{}: {}", d.where, d.message);
					if (d.severity == Severity::kError) {
						Widgets::Problem(line);
					} else {
						Widgets::Warn(line);
					}
				}
			}
		}

		void __stdcall RenderSetup()
		{
			auto& s = GetMutableSettings();
			RenderHeader(Manager::GetSingleton()->TakeSnapshot());
			for (const auto* page : { "Scope", "Runtime", "Diagnostics" }) {
				if (ImGui::CollapsingHeader(page, ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen)) {
					Group(s, page);
				}
			}
			SaveBar(s);
		}

		void __stdcall RenderLog()
		{
			if (!g_logRing) {
				ImGui::Text("no log buffer");
				return;
			}
			static char filter[64]{};
			ImGui::InputText("filter", filter, sizeof(filter));
			ImGui::SameLine();
			static bool autoScroll = true;
			ImGui::Checkbox("auto-scroll", &autoScroll);
			ImGui::BeginChild("log", ImVec2{ 0, 0 }, 1, 0);
			for (const auto& line : g_logRing->last_formatted(300)) {
				if (filter[0] && line.find(filter) == std::string::npos) {
					continue;
				}
				const bool warn = line.find("[warning]") != std::string::npos || line.find("[error]") != std::string::npos;
				if (warn) {
					Widgets::Warn(line);
				} else {
					ImGui::TextUnformatted(line.c_str());
				}
			}
			if (autoScroll) {
				ImGui::SetScrollHereY(1.0f);
			}
			ImGui::EndChild();
		}
	}

	// ----------------------------------------------------------------- header

	void RenderStatus(const Studio::Snapshot&)
	{
		const auto st = Manager::GetSingleton()->GetStatus();
		const auto store = GetRecipeStoreStatus();
		if (st.emissivePath) {
			Widgets::Ok("emissive path on");
		} else {
			Widgets::Problem("emissive path OFF");
		}
		ImGui::SameLine();
		if (st.layoutVerified) {
			Widgets::Ok("| layout verified");
		} else {
			Widgets::Warn("| layout unverified");
		}
		ImGui::SameLine();
		if (st.runtimeLab) {
			Widgets::Ok("| lab");
		} else {
			Widgets::Warn("| no lab");
		}
		ImGui::SameLine();
		ImGui::Text("| %u actor(s), %u piece(s), %u recipe(s), %u geometr%s, %u shell(s), %u light(s), tick %u ms | %zu recipe file(s), %zu with errors",
			st.actors, st.pieces, st.recipes, st.geometries, st.geometries == 1 ? "y" : "ies", st.shells, st.lights, st.tickMS, store.loaded, store.withErrors);
	}

	void SelectionCombo(const Studio::Snapshot& a_snapshot, const char* a_label)
	{
		auto&       selection = Studio::State().selection;
		const auto* piece = Studio::SelectedPiece(a_snapshot, selection);
		if (piece) {
			selection.actorID = piece->actorID;
			selection.armorID = piece->armorID;
			selection.firstPerson = piece->firstPerson;
		}
		const auto preview = piece ? std::format("{} / {} ({})", piece->actorName, piece->armorName, piece->firstPerson ? "1st" : "3rd") : std::string{ "nothing applied" };
		if (ImGui::BeginCombo(a_label, preview.c_str())) {
			std::size_t i = 0;
			for (const auto& p : a_snapshot.pieces) {
				const auto label = std::format("{} / {} ({})##sel{}", p.actorName, p.armorName, p.firstPerson ? "1st" : "3rd", i++);
				if (ImGui::Selectable(label.c_str(), &p == piece)) {
					// A new piece: the recipe, geometry, cell and region start over.
					selection = Studio::Selection{};
					selection.actorID = p.actorID;
					selection.armorID = p.armorID;
					selection.firstPerson = p.firstPerson;
				}
			}
			ImGui::EndCombo();
		}
	}

	void RecipeCombo(const Studio::PieceRow* a_piece, const char* a_label)
	{
		auto&       selection = Studio::State().selection;
		const auto* recipe = Studio::SelectedRecipe(a_piece, selection);
		if (ImGui::BeginCombo(a_label, recipe ? recipe->id.c_str() : "-")) {
			if (a_piece) {
				for (const auto& r : a_piece->recipes) {
					if (ImGui::Selectable(std::format("{} ({}, priority {})", r.id, r.key, r.priority).c_str(), &r == recipe)) {
						selection.recipeID = r.id;
						selection.output.reset();
						selection.layer.reset();
					}
				}
			}
			ImGui::EndCombo();
		}
	}

	void IsolateCheckbox(const Studio::RecipeRow* a_recipe, const char* a_label)
	{
		auto* manager = Manager::GetSingleton();
		auto& view = manager->Debug();
		bool  isolating = view.Isolating();
		if (ImGui::Checkbox(a_label, &isolating)) {
			manager->Isolate((isolating && a_recipe) ? a_recipe->id : std::string{}, -1, -1);
		}
		if (view.Isolating()) {
			auto text = "isolating " + view.isolateRecipe;
			if (view.isolateOutput >= 0) {
				text += std::format(" output {}", view.isolateOutput);
			}
			if (view.isolateLayer >= 0) {
				text += std::format(" layer {}", view.isolateLayer);
			}
			Widgets::Tooltip(text);
		}
	}

	void FreezeCheckbox(const Studio::RecipeRow* a_recipe, const char* a_label)
	{
		auto& view = Manager::GetSingleton()->Debug();
		if (ImGui::Checkbox(a_label, &view.freeze) && view.freeze && a_recipe) {
			view.scrubSeconds = a_recipe->time;  // freezing holds the moment, not the slider's old value
		}
	}

	void ScrubSlider(const Studio::RecipeRow* a_recipe, const char* a_label)
	{
		auto& view = Manager::GetSingleton()->Debug();
		// Running: the slider follows the clock. Frozen: it is the scrub. Taking
		// hold of it freezes at the moment grabbed, so a drag never fights the
		// clock.
		// The clock runs on unbounded; the slider shows it within the current
		// minute and moves it within that minute, so recipes that read `time`
		// never see a wrap.
		const float actual = view.freeze ? view.scrubSeconds : (a_recipe ? a_recipe->time : 0.0f);
		const float minute = std::floor(actual / 60.0f) * 60.0f;
		float       shown = actual - minute;
		const bool  changed = ImGui::SliderFloat(a_label, &shown, 0.0f, 60.0f, "%.2f");
		if (changed || ImGui::IsItemActive()) {
			view.freeze = true;
			view.scrubSeconds = minute + shown;
		}
		if (minute > 0.0f) {
			Widgets::Tooltip(std::format("minute {} of the clock; t = {:.2f} s", static_cast<int>(minute / 60.0f) + 1, actual));
		}
	}

	void RenderHeader(const Studio::Snapshot& a_snapshot)
	{
		RenderStatus(a_snapshot);
		const auto& selection = Studio::State().selection;
		const auto* piece = Studio::SelectedPiece(a_snapshot, selection);
		const auto* recipe = Studio::SelectedRecipe(piece, selection);
		ImGui::SetNextItemWidth(360.0f);
		SelectionCombo(a_snapshot, "Selection");
		if (piece && !piece->recipes.empty()) {
			ImGui::SameLine();
			ImGui::SetNextItemWidth(260.0f);
			RecipeCombo(piece, "Recipe");
		}
		FreezeCheckbox(recipe, "Freeze");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(220.0f);
		ScrubSlider(recipe, "t (s)");
		ImGui::SameLine();
		IsolateCheckbox(recipe, "Isolate this recipe");
		ImGui::Separator();
	}

	void RegisterMenu()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			logger::info("SKSE Menu Framework not installed; no in-game menu");
			return;
		}
		SKSEMenuFramework::SetSection(std::string{ Identity::kMenuTitle }.c_str());
		SKSEMenuFramework::AddSectionItem("Studio", Studio::RenderStudio);
		SKSEMenuFramework::AddSectionItem("Recipes", RenderRecipes);
		SKSEMenuFramework::AddSectionItem("Setup", RenderSetup);
		SKSEMenuFramework::AddSectionItem("Log", RenderLog);
		logger::info("SKSE Menu Framework pages registered");
	}
}
