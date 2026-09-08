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
#include <string_view>
#include <vector>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#include "extern/SKSEMenuFramework.h"
#pragma clang diagnostic pop

namespace ImGui = ImGuiMCP;
using ImGuiMCP::ImVec2;
using ImGuiMCP::ImGuiSliderFlags_Logarithmic;

namespace WornEnchantmentPBR
{
	namespace
	{
		namespace Widgets = Studio::Widgets;
		using Widgets::TableStyle;
		using Widgets::Width;

		bool     g_needsReapply = false;
		bool     g_autoReapply = true;
		Settings g_savedSettings{};
		bool     g_savedKnown = false;

		constexpr TableStyle kGridStyle{ .borders = TableStyle::Borders::kAll, .stretch = true, .headers = true, .rowBackground = true };

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

		constexpr const char* kSetupPages[]{ "Scope", "Runtime", "Diagnostics" };
		constexpr const char* kVerboseKey = "VerboseLogging";

		[[nodiscard]] bool Shown(const SettingDesc& d) noexcept
		{
			return std::ranges::any_of(kSetupPages, [&](const char* a_page) { return std::string_view{ d.page } == a_page; });
		}

		[[nodiscard]] const SettingDesc* FindSetting(std::string_view a_key) noexcept
		{
			const auto table = SettingTable();
			const auto it = std::ranges::find_if(table, [&](const SettingDesc& d) { return std::string_view{ d.key } == a_key; });
			return it == table.end() ? nullptr : &*it;
		}

		void Widget(Settings& s, const SettingDesc& d, const char* a_label)
		{
			using W = SettingDesc::Widget;
			bool changed = false;
			std::visit([&](auto a_member) {
				using T = std::remove_cvref_t<decltype(s.*a_member)>;
				if constexpr (std::is_same_v<T, bool>) {
					changed = ImGui::Checkbox(a_label, &(s.*a_member));
				} else if constexpr (std::is_same_v<T, float>) {
					changed = ImGui::SliderFloat(a_label, &(s.*a_member), d.min, d.max, d.max - d.min > 10 ? "%.2f" : "%.3f", d.widget == W::kLogSlider ? ImGuiSliderFlags_Logarithmic : 0);
				} else {
					if (d.widget == W::kEnum && d.items) {
						int v = static_cast<int>(s.*a_member);
						if (ImGui::Combo(a_label, &v, d.items)) {
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
						if (ImGui::Combo(a_label, &rel, sizes + lo, hi - lo + 1)) {
							s.*a_member = 64u << (lo + rel);
							changed = true;
						}
					} else {
						int v = static_cast<int>(s.*a_member);
						if (ImGui::SliderInt(a_label, &v, static_cast<int>(d.min), static_cast<int>(d.max))) {
							s.*a_member = static_cast<std::uint32_t>(v);
							changed = true;
						}
					}
				}
			},
				d.member);
			if (d.reapply) {
				MarkReapply(changed);
			}
		}

		void DrawSaveBar(Settings& s)
		{
			auto* manager = Manager::GetSingleton();
			if (!g_savedKnown) {
				g_savedSettings = s;
				g_savedKnown = true;
			}
			constexpr TableStyle style{ .borders = TableStyle::Borders::kAll, .stretch = false, .headers = false, .rowBackground = false };
			auto                 table = Widgets::Table::Begin("save-bar", { { "", Width::Fit() }, { "", Width::Fit() }, { "", Width::Fit() }, { "", Width::Fit() }, { "", Width::Fill() } }, style);
			if (!table.Open()) {
				return;
			}
			table.Cell();
			if (ImGui::Button("Save INI")) {
				if (SaveSettingsToDisk(s)) {
					g_savedSettings = s;
				}
			}
			table.Cell();
			if (ImGui::Button("Reload INI")) {
				SetSettings(LoadSettingsFromDisk());
				g_savedSettings = GetSettings();
				g_needsReapply = false;
				manager->ReapplyAll();
			}
			table.Cell();
			if (ImGui::Button("Re-apply")) {
				g_needsReapply = false;
				manager->ReapplyAll();
			}
			table.Cell();
			Widgets::Toggle("auto", g_autoReapply, "");
			Widgets::HelpMarker("Re-apply automatically when a setting that is read at apply time changes.");
			table.Cell();
			if (SettingsDiffer(s, g_savedSettings)) {
				Widgets::Warn("unsaved changes");
				ImGui::SameLine();
			}
			if (g_needsReapply) {
				Widgets::Warn("re-apply needed");
			}
			table.End();
		}

		void DrawValueTable(Settings& s)
		{
			auto table = Widgets::Table::Begin("values", { { "setting", Width::Fit() }, { "value", Width::Fill() } }, kGridStyle);
			if (!table.Open()) {
				return;
			}
			for (const auto& d : SettingTable()) {
				if (!Shown(d) || std::string_view{ d.key } == kVerboseKey) {
					continue;
				}
				ImGui::PushID(d.key);
				table.Cell();
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(d.label);
				Widgets::HelpMarker(d.help);
				table.Cell();
				Widgets::NextItemWidth(Width::Fill());
				Widget(s, d, "##value");
				ImGui::PopID();
			}
			table.End();
		}

		void DrawLog(Settings& s)
		{
			static char filter[64]{};
			static bool autoScroll = true;
			Widgets::NextItemWidth(Width::Px(240.0f));
			ImGui::InputText("filter", filter, sizeof(filter));
			ImGui::SameLine();
			Widgets::Toggle("auto-scroll", autoScroll, "");
			if (const auto* verbose = FindSetting(kVerboseKey)) {
				ImGui::SameLine();
				Widget(s, *verbose, verbose->label);
				Widgets::HelpMarker(verbose->help);
			}
			if (!g_logRing) {
				ImGui::Text("no log buffer");
				return;
			}
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

		void __stdcall RenderRecipes()
		{
			auto* manager = Manager::GetSingleton();
			manager->Watch(Studio::RequestOf(Studio::State().selection));
			const auto  held = manager->LatestSnapshot();
			const auto& snapshot = *held;
			RenderHeader(snapshot);
			const auto& selection = Studio::State().selection;

			if (ImGui::Button("Reload recipes")) {
				Studio::Reduce(Studio::State(), Studio::EndPaint{});
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
			auto table = Widgets::Table::Begin("recipes", { { "recipe", Width::Fill() }, { "keys", Width::Fill() }, { "rows", Width::Fill() }, { "state", Width::Fill() }, { "file", Width::Fill() } }, kGridStyle);
			if (table.Open()) {
				for (const auto& recipe : LoadedRecipes()) {
					if (IsTransient(recipe.id)) {
						continue;
					}
					table.Cell();
					ImGui::TextUnformatted(recipe.id.c_str());
					table.Cell();
					std::string keys;
					for (const auto& k : recipe.keys) {
						keys += (keys.empty() ? "" : ", ") + k.ToString();
					}
					ImGui::TextWrapped("%s", keys.c_str());
					table.Cell();
					ImGui::Text("%zu signals, %zu curves, %zu sources, %zu masks, %zu outputs", recipe.signals.size(), recipe.curves.size(), recipe.sources.size(), recipe.masks.size(), recipe.outputs.size());
					table.Cell();
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
					table.Cell();
					ImGui::TextWrapped("%s", origin ? origin->path.string().c_str() : "");
				}
				table.End();
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
			Manager::GetSingleton()->Watch(Studio::RequestOf(Studio::State().selection));
			const auto held = Manager::GetSingleton()->LatestSnapshot();
			RenderHeader(*held);
			const float half = ImGui::GetContentRegionAvail().y * 0.5f;
			if (ImGui::BeginChild("settings", ImVec2{ 0.0f, half }, 0, 0)) {
				DrawSaveBar(s);
				DrawValueTable(s);
			}
			ImGui::EndChild();
			Widgets::Rule();
			DrawLog(s);
		}
	}

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

	void RenderHeader(const Studio::Snapshot& a_snapshot)
	{
		RenderStatus(a_snapshot);
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
		logger::info("SKSE Menu Framework pages registered");
	}
}
