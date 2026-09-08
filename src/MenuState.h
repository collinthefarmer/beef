#pragma once

#include "Edits.h"
#include "TermStack.h"
#include "Studio.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>

namespace WornEnchantmentPBR::Studio
{
	using FieldKey = std::uint32_t;
	inline constexpr FieldKey kNoField = 0;

	enum class ResourceTab
	{
		kSignals,
		kCurves,
		kSources,
		kMasks,
	};
	inline constexpr std::array<ResourceTab, 4> kResourceTabs{ ResourceTab::kSignals, ResourceTab::kCurves, ResourceTab::kSources, ResourceTab::kMasks };
	[[nodiscard]] std::string_view              ResourceTabName(ResourceTab a_tab) noexcept;

	struct RegionStack
	{
		std::vector<Term>          terms;
		std::optional<std::size_t> selected;
		std::optional<std::size_t> solo;
		std::set<std::size_t>      muted;
		std::string                editing;
		bool                       dirty = false;
	};

	struct PaintSession
	{
		std::string recipe;
		Surface               surface = Surface::kMaterial;
		std::set<std::string> readGeometries;
	};

	using TextBuffer = std::array<char, 1024>;
	using NumberBuffer = std::array<float, 3>;

	struct MenuState
	{
		Mode      mode = Mode::kCompose;
		Layout    layout;
		Selection selection;
		bool settings = false;
		RegionStack                 region;
		std::optional<PaintSession> paint;
		ResourceTab resource = ResourceTab::kSignals;
		std::unordered_map<FieldKey, TextBuffer>   textBuffers;
		std::unordered_map<FieldKey, NumberBuffer> numberBuffers;
		FieldKey                                   activeField = kNoField;
		std::unordered_map<FieldKey, bool> comboMode;
		FieldKey                           focusField = kNoField;
	};

	[[nodiscard]] inline MenuState& State()
	{
		static MenuState state;
		return state;
	}

	struct SetMode
	{
		Mode mode = Mode::kCompose;
	};
	struct PickPiece
	{
		FormID actorID = 0;
		FormID armorID = 0;
		bool   firstPerson = false;
	};
	struct PickRecipe
	{
		std::string id;
	};
	struct PickTarget
	{
		Target target = Target::kMaterial;
	};
	struct PickSlot
	{
		Slot slot = Slot::kEmissive;
	};
	struct PickCell
	{
		Surface                    surface = Surface::kMaterial;
		Slot                       slot = Slot::kEmissive;
		std::optional<std::size_t> topLayer;
	};
	struct PickLayer
	{
		std::size_t index = 0;
	};
	struct ViewGeometry
	{
		std::string name;
	};
	struct ShowSettings
	{
		bool on = false;
	};
	struct ShowResource
	{
		ResourceTab tab = ResourceTab::kSignals;
	};
	struct AddTerm
	{
		Term term;
	};
	struct SetTermOp
	{
		std::size_t index = 0;
		TermOp      op = TermOp::kAnd;
	};
	struct SetTermText
	{
		std::size_t index = 0;
		std::string text;
	};
	struct SetTermKind
	{
		std::size_t index = 0;
		TermKind  kind;
		std::string text;
		std::string label;
	};
	struct RemoveTerm
	{
		std::size_t index = 0;
	};
	struct MoveTerm
	{
		std::size_t from = 0;
		std::size_t to = 0;
	};
	struct PickTerm
	{
		std::size_t index = 0;
	};
	struct SoloTerm
	{
		std::size_t index = 0;
		bool        on = false;
	};
	struct MuteTerm
	{
		std::size_t index = 0;
		bool        on = false;
	};
	struct LoadRegion
	{
		std::vector<Term> terms;
		std::string       editing;
	};
	struct ClearRegion
	{
	};
	struct BeginPaint
	{
		std::string recipe;
		RecipeKey   key;
		Surface     surface = Surface::kMaterial;
	};
	struct SetPaintSurface
	{
		Surface surface = Surface::kMaterial;
	};
	struct KeepPaint
	{
		std::string recipe;
		std::string name;
	};
	struct EndPaint
	{
	};
	struct ReadMesh
	{
		FormID      actorID = 0;
		std::string geometry;
	};
	struct EditRecipe
	{
		std::string recipe;
		RecipeEdit  edit;
	};
	struct SoloRecipe
	{
		std::string recipe;
		bool        on = false;
	};
	struct SoloOutput
	{
		std::string recipe;
		std::size_t output = 0;
		bool        on = false;
	};
	struct SoloLayer
	{
		std::string recipe;
		std::size_t output = 0;
		std::size_t layer = 0;
		bool        on = false;
	};
	struct MuteLayer
	{
		std::string recipe;
		std::size_t output = 0;
		std::size_t layer = 0;
		bool        on = false;
	};
	struct SetFreeze
	{
		bool  on = false;
		float at = 0.0f;
	};
	struct SetScrub
	{
		float seconds = 0.0f;
	};
	struct SetSpeed
	{
		float speed = 1.0f;
	};
	struct StepClock
	{
	};
	struct Undo
	{
		std::string recipe;
	};
	struct Redo
	{
		std::string recipe;
	};
	struct CreateRecipe
	{
		std::string id;
		RecipeKey   key;
		std::string geometry;
	};
	struct RenameRecipe
	{
		std::string from;
		std::string to;
	};
	struct FireTrigger
	{
		FormID      actorID = 0;
		std::string event;
		std::string node;
		Vec3        offset;
		float       random = 0.0f;
		float       value = 1.0f;
	};

	using Intent = std::variant<
		SetMode, PickPiece, PickRecipe, PickTarget, PickSlot, PickCell, PickLayer, ViewGeometry, ShowSettings, ShowResource, ReadMesh,
		AddTerm, SetTermOp, SetTermText, SetTermKind, RemoveTerm, MoveTerm, PickTerm, SoloTerm, MuteTerm, LoadRegion, ClearRegion,
		BeginPaint, SetPaintSurface, KeepPaint, EndPaint,
		EditRecipe, SoloRecipe, SoloOutput, SoloLayer, MuteLayer,
		SetFreeze, SetScrub, SetSpeed, StepClock, Undo, Redo, CreateRecipe, RenameRecipe, FireTrigger>;

	void Reduce(MenuState& a_state, const Intent& a_intent);
}
