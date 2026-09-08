#include "Recipe.h"

#include "Expression.h"
#include "Signals.h"
#include "Vocabulary.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <unordered_set>
#include <utility>

namespace WornEnchantmentPBR
{
	namespace
	{
		std::string_view Trim(std::string_view a_text) noexcept
		{
			while (!a_text.empty() && std::isspace(static_cast<unsigned char>(a_text.front()))) {
				a_text.remove_prefix(1);
			}
			while (!a_text.empty() && std::isspace(static_cast<unsigned char>(a_text.back()))) {
				a_text.remove_suffix(1);
			}
			return a_text;
		}

		bool EqualsIgnoringCase(std::string_view a, std::string_view b) noexcept
		{
			return a.size() == b.size() && std::ranges::equal(a, b, [](char x, char y) {
				return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
			});
		}

		struct SlotEntry
		{
			std::uint32_t    slot;
			std::string_view name;
		};
		constexpr SlotEntry kBipedSlots[]{
			{ 30, "head" }, { 31, "hair" }, { 32, "body" }, { 33, "hands" }, { 34, "forearms" }, { 35, "amulet" },
			{ 36, "ring" }, { 37, "feet" }, { 38, "calves" }, { 39, "shield" }, { 40, "tail" }
		};

		std::optional<std::string_view> RefOf(const Param& a_param) noexcept
		{
			const auto* ref = Get<Ref>(a_param);
			return ref ? std::optional<std::string_view>{ ref->name } : std::nullopt;
		}

		template <std::size_t N>
		void CollectRefs(const std::variant<std::array<Param, N>, Ref>& a_param, std::vector<std::string_view>& a_out)
		{
			Match(
				a_param,
				[&](const Ref& r) { a_out.push_back(r.name); },
				[&](const std::array<Param, N>& parts) {
					for (const auto& p : parts) {
						if (const auto r = RefOf(p)) {
							a_out.push_back(*r);
						}
					}
				});
		}
	}

	std::optional<FormKey> FormKey::Parse(std::string_view a_text)
	{
		a_text = Trim(a_text);
		if (a_text.size() < 4 || a_text[0] != '0' || (a_text[1] != 'x' && a_text[1] != 'X')) {
			return std::nullopt;
		}
		const auto sep = a_text.find('~');
		if (sep == std::string_view::npos || sep <= 2 || sep + 1 >= a_text.size()) {
			return std::nullopt;
		}
		const auto    hex = a_text.substr(2, sep - 2);
		std::uint32_t id = 0;
		const auto    r = std::from_chars(hex.data(), hex.data() + hex.size(), id, 16);
		if (r.ec != std::errc{} || r.ptr != hex.data() + hex.size() || hex.size() > 8) {
			return std::nullopt;
		}
		return FormKey{ std::string{ a_text.substr(sep + 1) }, id };
	}

	std::string FormKey::ToString() const
	{
		return std::format("0x{:X}~{}", localId, file);
	}

	bool FormKey::operator==(const FormKey& a_other) const noexcept
	{
		return localId == a_other.localId && EqualsIgnoringCase(file, a_other.file);
	}

	FormRef FormRef::From(std::string_view a_text)
	{
		FormRef ref;
		ref.text = std::string{ Trim(a_text) };
		ref.key = FormKey::Parse(ref.text);
		return ref;
	}

	std::optional<std::string> CurveRef::Named() const
	{
		if (text.size() > 1 && text[0] == '@' && IsName(std::string_view{ text }.substr(1))) {
			return text.substr(1);
		}
		return std::nullopt;
	}

	std::optional<SignalKind> DefaultSignalKind(std::string_view a_name)
	{
		const auto id = ParseSignalKind(a_name);
		return id ? AlternativeAt<SignalKind>(static_cast<std::size_t>(*id)) : std::nullopt;
	}

	SignalKindId SignalKindOf(const SignalKind& a_kind) noexcept
	{
		static_assert(std::variant_size_v<SignalKind> == kSignalKindCount);
		const std::size_t index = a_kind.index();
		return index < kSignalKindCount ? static_cast<SignalKindId>(index) : SignalKindId::kConstant;
	}

	std::string_view SignalKindName(SignalKindId a_kind) noexcept
	{
		return NameOf(kSignalKinds, a_kind);
	}

	std::optional<SignalKindId> ParseSignalKind(std::string_view a_name) noexcept
	{
		return FromName(kSignalKinds, a_name);
	}

	bool SignalKindTunable(SignalKindId a_kind) noexcept
	{
		const auto* row = RowOf(kSignalKinds, a_kind);
		return row && row->tunable;
	}

	std::string_view KeyKindName(KeyKind a_kind) noexcept
	{
		return NameOf(kKeyKinds, a_kind);
	}

	KeyOperand KeyOperandOf(KeyKind a_kind) noexcept
	{
		const auto* row = RowOf(kKeyKinds, a_kind);
		return row ? row->operand : KeyOperand::kForm;
	}

	int DefaultPriority(KeyKind a_kind) noexcept
	{
		const auto* row = RowOf(kKeyKinds, a_kind);
		return row ? row->priority : 0;
	}

	bool EnchantmentDerived(KeyKind a_kind) noexcept
	{
		const auto* row = RowOf(kKeyKinds, a_kind);
		return row && row->enchantmentDerived;
	}

	std::string_view RecipeKey::Glob() const noexcept
	{
		const auto* glob = Get<std::string>(operand);
		return glob ? std::string_view{ *glob } : std::string_view{};
	}

	std::string RecipeKey::ToString() const
	{
		return Match(
			operand,
			[&](const std::monostate&) { return std::string{ KeyKindName(kind) }; },
			[&](const FormRef& form) { return std::format("{}:{}", KeyKindName(kind), form.text); },
			[&](const std::string& glob) { return std::format("{}:{}", KeyKindName(kind), glob); });
	}

	std::string_view SelectorClause::Glob() const noexcept
	{
		const auto* glob = Get<std::string>(operand);
		return glob ? std::string_view{ *glob } : std::string_view{};
	}

	bool GlobMatch(std::string_view a_glob, std::string_view a_text) noexcept
	{
		const auto norm = [](char c) {
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return c == '\\' ? '/' : c;
		};
		std::size_t g = 0, t = 0, starG = std::string_view::npos, starT = 0;
		while (t < a_text.size()) {
			if (g < a_glob.size() && a_glob[g] == '*') {
				starG = g++;
				starT = t;
			} else if (g < a_glob.size() && norm(a_glob[g]) == norm(a_text[t])) {
				++g;
				++t;
			} else if (starG != std::string_view::npos) {
				g = starG + 1;
				t = ++starT;
			} else {
				return false;
			}
		}
		while (g < a_glob.size() && a_glob[g] == '*') {
			++g;
		}
		return g == a_glob.size();
	}

	bool Matches(const Selector& a_selector, const GeometryIdentity& a_geometry)
	{
		if (a_selector.All()) {
			return true;
		}
		return std::ranges::any_of(a_selector.anyOf, [&](const SelectorClause& t) {
			switch (t.kind) {
			case SelectorKind::kAddon: {
				const auto* form = t.Form();
				return form && form->key && a_geometry.addon && *a_geometry.addon == *form->key;
			}
			case SelectorKind::kGeometry:
				return GlobMatch(t.Glob(), a_geometry.name);
			case SelectorKind::kTexture:
				return GlobMatch(t.Glob(), a_geometry.diffusePath);
			}
			return false;
		});
	}

	std::optional<std::uint32_t> BipedSlotFromName(std::string_view a_name) noexcept
	{
		for (const auto& e : kBipedSlots) {
			if (EqualsIgnoringCase(e.name, a_name)) {
				return e.slot;
			}
		}
		if (!a_name.empty() && a_name.size() <= 2 && std::ranges::all_of(a_name, [](char c) { return c >= '0' && c <= '9'; })) {
			const std::uint32_t slot = static_cast<std::uint32_t>(std::stoul(std::string{ a_name }));
			if (slot >= 30 && slot <= 61) {
				return slot;
			}
		}
		return std::nullopt;
	}

	std::optional<std::string_view> BipedSlotName(std::uint32_t a_slot) noexcept
	{
		for (const auto& e : kBipedSlots) {
			if (e.slot == a_slot) {
				return e.name;
			}
		}
		return std::nullopt;
	}

	std::string_view SlotName(Slot a_slot) noexcept
	{
		return NameOf(kSlots, a_slot);
	}

	std::string_view SurfaceName(Surface a_surface) noexcept
	{
		return NameOf(kSurfaces, a_surface);
	}

	std::optional<Surface> ParseSurface(std::string_view a_name) noexcept
	{
		return FromName(kSurfaces, a_name);
	}

	std::string_view BlendName(Blend a_blend) noexcept
	{
		return NameOf(kBlends, a_blend);
	}

	std::optional<Blend> ParseBlend(std::string_view a_name) noexcept
	{
		return FromName(kBlends, a_name);
	}

	std::uint32_t BlendShaderMode(Blend a_blend) noexcept
	{
		const auto* row = RowOf(kBlends, a_blend);
		return row ? row->shaderMode : 0;
	}

	std::string_view MaterialChannelName(MaterialChannel a_channel) noexcept
	{
		return NameOf(kMaterialChannels, a_channel);
	}

	std::optional<MaterialChannel> ParseMaterialChannel(std::string_view a_name) noexcept
	{
		return FromName(kMaterialChannels, a_name);
	}

	std::string_view ImageChannelName(ImageChannel a_channel) noexcept
	{
		return NameOf(kImageChannels, a_channel);
	}

	std::optional<ImageChannel> ParseImageChannel(std::string_view a_name) noexcept
	{
		return FromName(kImageChannels, a_name);
	}

	ShaderChannel ShaderChannelOf(ImageChannel a_channel) noexcept
	{
		const auto* row = RowOf(kImageChannels, a_channel);
		return row ? row->channel : ShaderChannel::kRgb;
	}

	MaterialMap MaterialMapOf(MaterialChannel a_channel) noexcept
	{
		const auto* row = RowOf(kMaterialChannels, a_channel);
		return row ? row->map : MaterialMap::kNone;
	}

	ShaderChannel ShaderChannelOf(MaterialChannel a_channel) noexcept
	{
		const auto* row = RowOf(kMaterialChannels, a_channel);
		return row ? row->channel : ShaderChannel::kR;
	}

	ValueType MaterialChannelType(MaterialChannel a_channel) noexcept
	{
		const auto* row = RowOf(kMaterialChannels, a_channel);
		return row ? row->type : ValueType::kScalar;
	}

	bool Thresholdable(MaterialChannel a_channel) noexcept
	{
		return RowOf(kMaterialChannels, a_channel) && MaterialChannelType(a_channel) == ValueType::kScalar;
	}

	std::string_view ImageSpaceName(ImageSpace a_space) noexcept
	{
		return NameOf(kImageSpaces, a_space);
	}

	std::optional<ImageSpace> ParseImageSpace(std::string_view a_name) noexcept
	{
		return FromName(kImageSpaces, a_name);
	}

	std::string_view UvAxisName(UvAxis a_axis) noexcept
	{
		return NameOf(kUvAxes, a_axis);
	}

	std::optional<UvAxis> ParseUvAxis(std::string_view a_name) noexcept
	{
		return FromName(kUvAxes, a_name);
	}

	std::string_view RippleShapeName(RippleShape a_shape) noexcept
	{
		return NameOf(kRippleShapes, a_shape);
	}

	std::optional<RippleShape> ParseRippleShape(std::string_view a_name) noexcept
	{
		return FromName(kRippleShapes, a_name);
	}

	namespace
	{
		constexpr Slot kEverySlot[]{ Slot::kDiffuse, Slot::kEmissive, Slot::kRmaos, Slot::kNormal, Slot::kHeight, Slot::kFuzz, Slot::kGlint, Slot::kCoat, Slot::kSubsurface };
		static_assert(std::size(kEverySlot) == kSlotCount);
		constexpr Slot kVanillaShellSlots[]{ Slot::kEmissive };

		bool Contains(std::span<const Slot> a_slots, Slot a_slot) noexcept
		{
			return std::ranges::find(a_slots, a_slot) != a_slots.end();
		}
	}

	std::string_view ScalarFieldName(ScalarField a_field) noexcept
	{
		return NameOf(kScalarFields, a_field);
	}

	std::optional<ScalarField> ParseScalarField(std::string_view a_name) noexcept
	{
		return FromName(kScalarFields, a_name);
	}

	float ScalarFallback(ScalarField a_field) noexcept
	{
		const auto* row = RowOf(kScalarFields, a_field);
		return row ? row->fallback : 0.0f;
	}

	MaterialMap BaseMapOf(Slot a_slot) noexcept
	{
		const auto* row = RowOf(kSlots, a_slot);
		return row ? row->baseMap : MaterialMap::kNone;
	}

	std::span<const Slot> SlotsOf(Surface a_surface, ShellMaterial a_shell) noexcept
	{
		if (a_surface == Surface::kShell && a_shell == ShellMaterial::kVanilla) {
			return kVanillaShellSlots;
		}
		return kEverySlot;
	}

	bool SurfaceHasSlot(Surface a_surface, ShellMaterial a_shell, Slot a_slot) noexcept
	{
		return Contains(SlotsOf(a_surface, a_shell), a_slot);
	}

	std::span<const ScalarField> ScalarsOf(Slot a_slot) noexcept
	{
		const auto* row = RowOf(kSlots, a_slot);
		return row ? row->scalars : std::span<const ScalarField>{};
	}

	bool ScalarRequired(Slot a_slot, ScalarField a_field) noexcept
	{
		const auto* row = RowOf(kSlots, a_slot);
		return row && row->scalarsRequired && std::ranges::find(row->scalars, a_field) != row->scalars.end();
	}

	bool SlotsExclude(Slot a_first, Slot a_second) noexcept
	{
		const auto* row = RowOf(kSlots, a_first);
		return row && Contains(row->excludes, a_second);
	}

	bool BlendAllowed(Slot a_slot, Blend a_blend) noexcept
	{
		const auto* row = RowOf(kBlends, a_blend);
		return row && (!row->normalStackOnly || a_slot == Slot::kNormal);
	}

	ChannelSet ChannelsOf(Slot a_slot) noexcept
	{
		const auto* row = RowOf(kSlots, a_slot);
		return row ? row->channels : ChannelSet{};
	}

	std::string_view SlotChannelNote(Slot a_slot) noexcept
	{
		const auto* row = RowOf(kSlots, a_slot);
		return row ? row->note : "";
	}

	std::optional<Param>* ScalarOf(SlotScalars& a_scalars, ScalarField a_field) noexcept
	{
		const auto* row = RowOf(kScalarFields, a_field);
		if (!row) {
			return nullptr;
		}
		const auto* member = Get<std::optional<Param> SlotScalars::*>(row->member);
		return member ? &(a_scalars.**member) : nullptr;
	}

	const std::optional<Param>* ScalarOf(const SlotScalars& a_scalars, ScalarField a_field) noexcept
	{
		return ScalarOf(const_cast<SlotScalars&>(a_scalars), a_field);
	}

	std::optional<ChannelSet> ChannelSet::Parse(std::string_view a_text)
	{
		ChannelSet set{ false, false, false, false };
		if (a_text.empty() || a_text.size() > 4) {
			return std::nullopt;
		}
		for (const char c : a_text) {
			switch (std::tolower(static_cast<unsigned char>(c))) {
			case 'r':
				set.r = true;
				break;
			case 'g':
				set.g = true;
				break;
			case 'b':
				set.b = true;
				break;
			case 'a':
				set.a = true;
				break;
			default:
				return std::nullopt;
			}
		}
		return set;
	}

	std::string ChannelSet::ToString() const
	{
		std::string out;
		if (r) out += 'r';
		if (g) out += 'g';
		if (b) out += 'b';
		if (a) out += 'a';
		return out;
	}

	const Signal* Recipe::FindSignal(std::string_view a_name) const noexcept
	{
		const auto it = std::ranges::find(signals, a_name, &Signal::name);
		return it == signals.end() ? nullptr : &*it;
	}

	const Curve* Recipe::FindCurve(std::string_view a_name) const noexcept
	{
		const auto it = std::ranges::find(curves, a_name, &Curve::name);
		return it == curves.end() ? nullptr : &*it;
	}

	const Source* Recipe::FindSource(std::string_view a_name) const noexcept
	{
		const auto it = std::ranges::find(sources, a_name, &Source::name);
		return it == sources.end() ? nullptr : &*it;
	}

	const Mask* Recipe::FindMask(std::string_view a_name) const noexcept
	{
		const auto it = std::ranges::find(masks, a_name, &Mask::name);
		return it == masks.end() ? nullptr : &*it;
	}

	bool LoadResult::HasErrors() const noexcept
	{
		return !recipe || std::ranges::any_of(diagnostics, [](const Diagnostic& d) { return d.severity == Severity::kError; });
	}

	namespace
	{
		class RowTypes
		{
		public:
			RowTypes(const Recipe& a_recipe, const SignalGraph& a_graph) :
				recipe_(a_recipe), graph_(a_graph) {}

			std::optional<ValueType> MaterialTexel(std::string_view a_name, std::unordered_set<std::string>& a_visiting) const
			{
				if (const auto* source = recipe_.FindSource(a_name)) {
					return SourceType(*source);
				}
				if (const auto* mask = recipe_.FindMask(a_name)) {
					return MaskType(*mask, a_visiting);
				}
				return graph_.TypeOf(a_name);
			}

			static ValueType SourceType(const Source& a_source)
			{
				return WornEnchantmentPBR::SourceType(a_source);
			}

			std::optional<ValueType> MaskType(const Mask& a_mask, std::unordered_set<std::string>& a_visiting) const
			{
				if (!a_visiting.insert(a_mask.name).second) {
					return std::nullopt;
				}
				const auto program = Program::Parse(a_mask.text);
				if (!program) {
					a_visiting.erase(a_mask.name);
					return std::nullopt;
				}
				const auto type = program->Check([&](std::string_view name) { return MaterialTexel(name, a_visiting); });
				a_visiting.erase(a_mask.name);
				return type ? std::optional{ *type } : std::nullopt;
			}

		private:
			const Recipe&      recipe_;
			const SignalGraph& graph_;
		};

		class Validator
		{
		public:
			explicit Validator(const Recipe& a_recipe) :
				recipe_(a_recipe), graph_(SignalGraph::Compile(a_recipe.signals, a_recipe.curves)), types_(a_recipe, graph_) {}

			std::vector<Diagnostic> Run()
			{
				for (const auto& d : graph_.Diagnostics()) {
					out_.push_back(d);
				}
				UniqueNames();
				Curves();
				Sources();
				Masks();
				Outputs();
				Shell();
				Variants();
				return std::move(out_);
			}

		private:
			void Error(std::string a_where, std::string a_message) { out_.push_back({ Severity::kError, std::move(a_where), std::move(a_message) }); }
			void Warn(std::string a_where, std::string a_message) { out_.push_back({ Severity::kWarning, std::move(a_where), std::move(a_message) }); }

			template <class Row>
			void Unique(const std::vector<Row>& a_rows, const char* a_what)
			{
				std::unordered_set<std::string> seen;
				for (const auto& row : a_rows) {
					if (!IsName(row.name)) {
						Error(std::format("{} '{}'", a_what, row.name), "names are letters, digits and underscores, not starting with a digit");
					} else if (!seen.insert(row.name).second) {
						Error(std::format("{} {}", a_what, row.name), "duplicate name");
					}
				}
			}

			void UniqueNames()
			{
				Unique(recipe_.signals, "signal");
				Unique(recipe_.curves, "curve");
				Unique(recipe_.sources, "source");
				Unique(recipe_.masks, "mask");
				std::unordered_set<std::string> images;
				for (const auto& s : recipe_.sources) {
					images.insert(s.name);
				}
				for (const auto& m : recipe_.masks) {
					if (!images.insert(m.name).second) {
						Error(std::format("mask {}", m.name), "a source has the same name; per-texel expressions read both by name");
					}
				}
				for (const auto& s : recipe_.signals) {
					if (images.contains(s.name)) {
						Warn(std::format("signal {}", s.name), "a source or mask has the same name; inside masks the image wins");
					}
				}
				std::unordered_set<std::string> variants;
				for (const auto& v : recipe_.variants) {
					if (v.name.empty()) {
						Error("variant", "has no name");
					} else if (!variants.insert(v.name).second) {
						Error(std::format("variant {}", v.name), "duplicate name");
					}
				}
			}

			void Scalar(const std::string& a_where, const Param& a_param, std::string_view a_field)
			{
				const auto name = RefOf(a_param);
				if (!name) {
					return;
				}
				const auto type = graph_.TypeOf(*name);
				if (!type) {
					Error(a_where, std::format("'{}' reads unknown signal '@{}'", a_field, *name));
				} else if (*type != ValueType::kScalar) {
					Error(a_where, std::format("'{}' must be a scalar; '@{}' is a {}", a_field, *name, Name(*type)));
				}
			}

			template <std::size_t N>
			void Vector(const std::string& a_where, const std::variant<std::array<Param, N>, Ref>& a_param, const char* a_field, bool a_color = false)
			{
				const ValueType want = N == 2 ? ValueType::kVec2 : ValueType::kVec3;
				Match(
					a_param,
					[&](const Ref& r) {
						const auto type = graph_.TypeOf(r.name);
						if (!type) {
							Error(a_where, std::format("'{}' reads unknown signal '@{}'", a_field, r.name));
						} else if (*type != want) {
							Error(a_where, std::format("'{}' must be a {}; '@{}' is a {}", a_field, Name(want), r.name, Name(*type)));
						}
					},
					[&](const std::array<Param, N>& parts) {
						for (const auto& p : parts) {
							Scalar(a_where, p, a_field);
							if (const auto* number = Get<float>(p); a_color && number && (*number < 0.0f || *number > 1.0f)) {
								Error(a_where, std::format("'{}' components are 0..1", a_field));
							}
						}
					});
			}

			void Curve(const std::string& a_where, const std::optional<CurveRef>& a_curve)
			{
				if (!a_curve) {
					return;
				}
				if (const auto name = a_curve->Named()) {
					if (!recipe_.FindCurve(*name)) {
						Error(a_where, std::format("'curve' names unknown curve '@{}'", *name));
					}
					return;
				}
				if (const auto program = ParseCurve(a_curve->text); !program) {
					Error(a_where, std::format("curve: {}", program.error()));
				}
			}

			void Trigger(const std::string& a_where, const Ref& a_ref, const char* a_field)
			{
				const auto* s = recipe_.FindSignal(a_ref.name);
				if (!s) {
					Error(a_where, std::format("'{}' names unknown signal '@{}'", a_field, a_ref.name));
				} else if (!Is<TriggerSignal>(s->kind)) {
					Error(a_where, std::format("'{}' must name a trigger; '@{}' is not one", a_field, a_ref.name));
				}
			}

			void Curves()
			{
				for (const auto& c : recipe_.curves) {
					if (const auto program = ParseCurve(c.text); !program) {
						Error(std::format("curve {}", c.name), program.error());
					}
				}
			}

			void Sources()
			{
				for (const auto& src : recipe_.sources) {
					const auto where = std::format("source {}", src.name);
					Match(
						src.kind,
						[&](const ImageSource& s) {
							if (s.path.empty()) {
								Error(where, "'path' is empty");
							}
							if (s.scroll) {
								Vector(where, *s.scroll, "scroll");
							}
							if (s.tile) {
								Vector(where, *s.tile, "tile");
							}
						},
						[&](const BakeSource& s) {
							if (const auto* bw = Get<BoneWeightBake>(s.bake); bw && bw->bones.empty()) {
								Error(where, "boneWeight needs at least one bone");
							}
						},
						[&](const DistanceSource& s) {
							if (const auto* node = Get<std::string>(s.from); node && node->empty()) {
								Error(where, "'distance' needs a node name or a point");
							}
						},
						[&](const RippleSource& s) {
							Trigger(where, s.trigger, "trigger");
							Scalar(where, s.speed, "speed");
							Scalar(where, s.width, "width");
							Scalar(where, s.decay, "decay");
						},
						[&](const MaterialClustersSource& s) {
							if (s.clusters < 1 || s.clusters > kMaxMaterialClusters) {
								Error(where, std::format("'clusters' is 1..{}", kMaxMaterialClusters));
							}
							if (s.iterations < 1 || s.iterations > kMaxClusterIterations) {
								Error(where, std::format("'iterations' is 1..{}", kMaxClusterIterations));
							}
							for (const auto& [weight, field] : { std::pair{ s.roughness, "roughness" }, std::pair{ s.metallic, "metallic" }, std::pair{ s.occlusion, "occlusion" }, std::pair{ s.reflectance, "reflectance" }, std::pair{ s.luma, "luma" } }) {
								if (!(weight >= 0.0f && weight <= kMaxChannelWeight)) {
									Error(where, std::format("'weights.{}' is 0..{}", field, kMaxChannelWeight));
								}
							}
						},
						[](const auto&) {});
				}
			}

			void Masks()
			{
				for (const auto& m : recipe_.masks) {
					const auto where = std::format("mask {}", m.name);
					const auto program = Program::Parse(m.text);
					if (!program) {
						Error(where, program.error());
						continue;
					}
					if (program->UsesX()) {
						Error(where, "'x' is only defined inside a curve");
					}
					if (std::ranges::find(program->References(), m.name) != program->References().end()) {
						Error(where, "reads itself");
						continue;
					}
					std::unordered_set<std::string> visiting{ m.name };
					const auto                      type = program->Check([&](std::string_view name) { return types_.MaterialTexel(name, visiting); });
					if (!type) {
						Error(where, type.error());
					}
					for (const auto& curve : program->Curves()) {
						if (!recipe_.FindCurve(curve)) {
							Error(where, std::format("calls unknown curve '@{}'", curve));
						}
					}
				}
			}

			void Layer(const std::string& a_where, const WornEnchantmentPBR::Layer& a_layer, Slot a_slot)
			{
				if (const auto* ref = Get<Ref>(a_layer.source)) {
					if (!recipe_.FindSource(ref->name) && !recipe_.FindMask(ref->name)) {
						Error(a_where, std::format("'source' names unknown source or mask '@{}'", ref->name));
					}
				}
				Curve(a_where, a_layer.curve);
				Scalar(a_where, a_layer.opacity, "opacity");
				if (a_layer.color) {
					Vector(a_where, *a_layer.color, "color", true);
				}
				if (a_layer.mask && !recipe_.FindMask(a_layer.mask->name)) {
					Error(a_where, std::format("'mask' names unknown mask '@{}'", a_layer.mask->name));
				}
				if (!BlendAllowed(a_slot, a_layer.blend)) {
					Error(a_where, std::format("blend '{}' is valid only on the normal stack", BlendName(a_layer.blend)));
				}
			}

			void Scalars(const std::string& a_where, const SurfaceOutput& a_output)
			{
				for (const auto field : ScalarsOf(a_output.slot)) {
					const auto name = ScalarFieldName(field);
					if (field == ScalarField::kColor) {
						if (a_output.scalars.color) {
							Vector(a_where, *a_output.scalars.color, "color", true);
						} else if (ScalarRequired(a_output.slot, field)) {
							Error(a_where, std::format("slot '{}' needs '{}'", SlotName(a_output.slot), name));
						}
						continue;
					}
					const std::optional<Param>* param = ScalarOf(a_output.scalars, field);
					if (!param) {
						continue;
					}
					if (*param) {
						Scalar(a_where, **param, name);
					} else if (ScalarRequired(a_output.slot, field)) {
						Error(a_where, std::format("slot '{}' needs '{}'", SlotName(a_output.slot), name));
					}
				}
			}

			void Outputs()
			{
				std::map<Surface, std::vector<Slot>> bound;
				std::size_t                          index = 0;
				for (const auto& o : recipe_.outputs) {
					const auto where = std::format("output {}", index++);
					Match(
						o,
						[&](const SurfaceOutput& m) {
							Scalars(where, m);
							if (!SurfaceHasSlot(m.surface, recipe_.shell.material, m.slot)) {
								Error(where, std::format("a vanilla shell has only the emissive slot; '{}' is not one (its diffuse and normal maps are never written; rim and emissive strength are shell scalars)", SlotName(m.slot)));
							}
							auto& held = bound[m.surface];
							for (const auto other : held) {
								if (SlotsExclude(other, m.slot)) {
									Warn(where, std::format("'{}' and '{}' on the same material exclude each other; this output is dropped", SlotName(other), SlotName(m.slot)));
								}
							}
							held.push_back(m.slot);
							std::size_t li = 0;
							for (const auto& l : m.stack) {
								Layer(std::format("{} layer {}", where, li++), l, m.slot);
							}
						},
						[&](const LightOutput& l) {
							Vector(where, l.offset, "offset");
							Vector(where, l.color, "color", true);
							Scalar(where, l.intensity, "intensity");
							Scalar(where, l.size, "size");
							Scalar(where, l.cutoff, "cutoff");
							if (const auto* named = Get<NamedBones>(l.bones); named && named->bones.empty()) {
								Error(where, "'named' needs at least one bone");
							}
							if (const auto* skinned = Get<SkinnedBones>(l.bones); skinned && skinned->max == 0) {
								Error(where, "'skinned.max' must be at least 1");
							}
						});
				}
			}

			void Shell()
			{
				const auto& s = recipe_.shell;
				Scalar("shell", s.alpha, "alpha");
				Scalar("shell", s.rimPower, "rimPower");
				Scalar("shell", s.emissive, "emissive");
				Vector("shell pose", s.pose.inflate, "inflate");
				Vector("shell pose", s.pose.offset, "offset");
				Scalar("shell pose", s.pose.scale, "scale");
				Scalar("shell pose", s.pose.spin, "spin");
			}

			void Variants()
			{
				for (const auto& v : recipe_.variants) {
					const auto where = std::format("variant {}", v.name);
					for (const auto& [name, value] : v.overrides) {
						const auto type = graph_.TypeOf(name);
						if (!type) {
							Error(where, std::format("overrides unknown signal '{}'", name));
						} else if (*type != TypeOf(value)) {
							Error(where, std::format("override of '{}' is a {}; the signal is a {}", name, Name(TypeOf(value)), Name(*type)));
						}
					}
				}
			}

			const Recipe&           recipe_;
			SignalGraph             graph_;
			RowTypes                types_;
			std::vector<Diagnostic> out_;
		};
	}

	std::vector<Diagnostic> Validate(const Recipe& a_recipe)
	{
		return Validator{ a_recipe }.Run();
	}

	namespace
	{
		bool KeyMatches(const RecipeKey& a_key, const WornPiece& a_piece)
		{
			const auto* row = RowOf(kKeyKinds, a_key.kind);
			if (!row) {
				return false;
			}
			switch (row->operand) {
			case KeyOperand::kNone:
				return true;
			case KeyOperand::kGlob:
				return std::ranges::any_of(a_piece.diffusePaths, [&](const std::string& p) { return GlobMatch(a_key.Glob(), p); });
			case KeyOperand::kForm: {
				const auto* form = a_key.Form();
				if (!form || !form->key) {
					return false;
				}
				if (row->singleForm) {
					const auto& have = a_piece.*row->singleForm;
					return have && *have == *form->key;
				}
				if (row->formList) {
					return std::ranges::any_of(a_piece.*row->formList, [&](const FormKey& k) { return k == *form->key; });
				}
				return false;
			}
			}
			return false;
		}
	}

	std::vector<ResolvedRecipe> Resolve(const WornPiece& a_piece, std::span<const Recipe> a_loaded)
	{
		struct Candidate
		{
			ResolvedRecipe resolved;
			std::size_t    loadIndex;
		};
		std::vector<RecipeKey> claimed;
		std::vector<Candidate> matches;
		bool                   enchantmentMatched = false;
		for (std::size_t i = a_loaded.size(); i-- > 0;) {
			const auto&              recipe = a_loaded[i];
			std::optional<RecipeKey> best;
			for (const auto& key : recipe.keys) {
				if (std::ranges::find(claimed, key) != claimed.end()) {
					continue;
				}
				claimed.push_back(key);
				if (!KeyMatches(key, a_piece)) {
					continue;
				}
				if (!best || DefaultPriority(key.kind) > DefaultPriority(best->kind)) {
					best = key;
				}
			}
			if (!best) {
				continue;
			}
			enchantmentMatched = enchantmentMatched || EnchantmentDerived(best->kind);
			matches.push_back({ { &recipe, *best, recipe.priority.value_or(DefaultPriority(best->kind)) }, i });
		}
		if (enchantmentMatched) {
			std::erase_if(matches, [](const Candidate& m) { return m.resolved.key.kind == KeyKind::kDefault; });
		}
		std::ranges::stable_sort(matches, [](const Candidate& a, const Candidate& b) {
			if (a.resolved.priority != b.resolved.priority) {
				return a.resolved.priority < b.resolved.priority;
			}
			return a.loadIndex < b.loadIndex;
		});
		std::vector<ResolvedRecipe> out;
		out.reserve(matches.size());
		for (const auto& m : matches) {
			out.push_back(m.resolved);
		}
		return out;
	}

	bool AnyUnenchantedKey(std::span<const Recipe> a_loaded) noexcept
	{
		return std::ranges::any_of(a_loaded, [](const Recipe& r) {
			return std::ranges::any_of(r.keys, [](const RecipeKey& k) {
				const auto* row = RowOf(kKeyKinds, k.kind);
				return row && row->operand != KeyOperand::kNone && !row->enchantmentDerived;
			});
		});
	}

	std::vector<PieceKey> KeyChoicesOf(const WornPiece& a_piece)
	{
		std::vector<PieceKey> out;
		for (std::size_t i = std::size(kKeyKinds); i-- > 0;) {
			const auto& row = kKeyKinds[i];
			if (row.operand != KeyOperand::kForm) {
				continue;
			}
			if (row.singleForm) {
				if (const auto& form = a_piece.*row.singleForm) {
					out.push_back({ row.value, *form });
				}
			} else if (row.formList) {
				for (const auto& form : a_piece.*row.formList) {
					out.push_back({ row.value, form });
				}
			}
		}
		return out;
	}

	bool VariantApplies(const Variant& a_variant, const FormKey& a_armor) noexcept
	{
		const auto* armor = Get<FormRef>(a_variant.key);
		return armor && armor->key && *armor->key == a_armor;
	}

	bool VariantApplies(const Variant& a_variant, const GeometryIdentity& a_geometry)
	{
		const auto* selector = Get<Selector>(a_variant.key);
		return selector && Matches(*selector, a_geometry);
	}

	Recipe ApplyVariant(const Recipe& a_recipe, const Variant& a_variant)
	{
		Recipe out = a_recipe;
		for (auto& s : out.signals) {
			const auto it = a_variant.overrides.find(s.name);
			if (it == a_variant.overrides.end()) {
				continue;
			}
			s.kind = ConstantSignal{ it->second };
			s.curve.reset();
		}
		return out;
	}

	std::string_view ShellMaterialName(ShellMaterial a_material) noexcept
	{
		return NameOf(kShellMaterials, a_material);
	}

	std::optional<ShellMaterial> ParseShellMaterial(std::string_view a_name) noexcept
	{
		return FromName(kShellMaterials, a_name);
	}

	std::string_view ShellBlendName(ShellBlend a_blend) noexcept
	{
		return NameOf(kShellBlends, a_blend);
	}

	std::optional<ShellBlend> ParseShellBlend(std::string_view a_name) noexcept
	{
		return FromName(kShellBlends, a_name);
	}

	bool IsName(std::string_view a_text) noexcept
	{
		if (a_text.empty() || !(std::isalpha(static_cast<unsigned char>(a_text[0])) || a_text[0] == '_')) {
			return false;
		}
		return std::ranges::all_of(a_text, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
	}

	namespace
	{
		class AnimationQuery
		{
		public:
			explicit AnimationQuery(const Recipe& a_recipe) :
				recipe_(a_recipe) {}

			bool Signal(std::string_view a_name)
			{
				const auto* signal = recipe_.FindSignal(a_name);
				if (!signal) {
					return false;
				}
				return Guarded("s:" + std::string{ a_name }, [&] {
					return Match(
						signal->kind,
						[](const ConstantSignal&) { return false; },
						[&](const ExprSignal& e) {
							const auto program = Program::Parse(e.text);
							if (!program) {
								return false;
							}
							if (program->UsesTime()) {
								return true;
							}
							return std::ranges::any_of(program->References(), [&](const std::string& r) { return Signal(r); });
						},
						[&](const GradientSignal& g) {
							bool any = Param(g.t);
							for (const auto& stop : g.stops) {
								any = any || Vector(stop.color);
							}
							return any;
						},
						[&](const DeltaSignal& d) { return Signal(d.of.name); },
						[&](const SmoothSignal& s) { return Signal(s.of.name); },
						[](const auto&) { return true; });
				});
			}

			bool Param(const WornEnchantmentPBR::Param& a_param)
			{
				const auto name = RefOf(a_param);
				return name && Signal(*name);
			}

			template <std::size_t N>
			bool Vector(const std::variant<std::array<WornEnchantmentPBR::Param, N>, Ref>& a_param)
			{
				std::vector<std::string_view> refs;
				CollectRefs(a_param, refs);
				return std::ranges::any_of(refs, [&](std::string_view r) { return Signal(r); });
			}

			bool Source(std::string_view a_name)
			{
				const auto* source = recipe_.FindSource(a_name);
				if (!source) {
					return false;
				}
				return Guarded("r:" + std::string{ a_name }, [&] {
					return Match(
						source->kind,
						[&](const ImageSource& s) { return (s.scroll && Vector(*s.scroll)) || (s.tile && Vector(*s.tile)); },
						[](const RippleSource&) { return true; },
						[](const auto&) { return false; });
				});
			}

			bool Mask(std::string_view a_name)
			{
				const auto* mask = recipe_.FindMask(a_name);
				if (!mask) {
					return false;
				}
				return Guarded("m:" + std::string{ a_name }, [&] {
					const auto program = Program::Parse(mask->text);
					if (!program) {
						return false;
					}
					if (program->UsesTime()) {
						return true;
					}
					return std::ranges::any_of(program->References(), [&](const std::string& r) { return Image(r); });
				});
			}

			bool Image(std::string_view a_name)
			{
				if (recipe_.FindSource(a_name)) {
					return Source(a_name);
				}
				if (recipe_.FindMask(a_name)) {
					return Mask(a_name);
				}
				return Signal(a_name);
			}

		private:
			template <class F>
			bool Guarded(const std::string& a_key, F a_f)
			{
				if (!visiting_.insert(a_key).second) {
					return false;
				}
				const bool result = a_f();
				visiting_.erase(a_key);
				return result;
			}

			const Recipe&                   recipe_;
			std::unordered_set<std::string> visiting_;
		};
	}

	bool IsAnimated(const Recipe& a_recipe, std::string_view a_signal)
	{
		return AnimationQuery{ a_recipe }.Signal(a_signal);
	}

	bool IsAnimated(const Recipe& a_recipe, const Source& a_source)
	{
		return AnimationQuery{ a_recipe }.Source(a_source.name);
	}

	bool IsAnimated(const Recipe& a_recipe, const Mask& a_mask)
	{
		return AnimationQuery{ a_recipe }.Mask(a_mask.name);
	}

	bool IsAnimated(const Recipe& a_recipe, const Output& a_output)
	{
		AnimationQuery q{ a_recipe };
		return Match(
			a_output,
			[&](const LightOutput& l) {
				return q.Vector(l.color) || q.Param(l.intensity) || q.Param(l.size) || q.Param(l.cutoff) || q.Vector(l.offset);
			},
			[&](const SurfaceOutput& m) {
				for (const auto& l : m.stack) {
					if (const auto* ref = Get<Ref>(l.source); ref && q.Image(ref->name)) {
						return true;
					}
					if (q.Param(l.opacity) || (l.color && q.Vector(*l.color)) || (l.mask && q.Mask(l.mask->name))) {
						return true;
					}
				}
				return false;
			});
	}

	namespace
	{
		std::string TrimCopy(std::string_view a_text)
		{
			const auto begin = a_text.find_first_not_of(" \t");
			const auto end = a_text.find_last_not_of(" \t");
			return begin == std::string_view::npos ? std::string{} : std::string{ a_text.substr(begin, end - begin + 1) };
		}

		std::string NumberText(float a_value)
		{
			auto text = std::format("{:.4f}", a_value);
			while (text.ends_with('0')) {
				text.pop_back();
			}
			if (text.ends_with('.')) {
				text.pop_back();
			}
			return text;
		}

		std::optional<float> ParseNumber(std::string_view a_text)
		{
			const auto text = TrimCopy(a_text);
			float      value = 0.0f;
			const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
			return result.ec == std::errc{} && result.ptr == text.data() + text.size() ? std::optional{ value } : std::nullopt;
		}

		std::vector<std::string> SplitCommas(std::string_view a_text)
		{
			std::vector<std::string> parts;
			std::size_t              start = 0;
			while (start <= a_text.size()) {
				const auto comma = a_text.find(',', start);
				parts.push_back(TrimCopy(a_text.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start)));
				if (comma == std::string_view::npos) {
					break;
				}
				start = comma + 1;
			}
			return parts;
		}
	}

	std::string ParamText(const Param& a_param)
	{
		return Match(
			a_param,
			[](float f) { return NumberText(f); },
			[](const Ref& r) { return "@" + r.name; });
	}

	std::optional<Param> ParseParam(std::string_view a_text)
	{
		const auto text = TrimCopy(a_text);
		if (text.starts_with('@') && text.size() > 1) {
			return Param{ Ref{ text.substr(1) } };
		}
		if (const auto number = ParseNumber(text)) {
			return Param{ *number };
		}
		return std::nullopt;
	}

	std::string Vec3ParamText(const Vec3Param& a_param)
	{
		return Match(
			a_param,
			[](const Ref& r) { return "@" + r.name; },
			[](const std::array<Param, 3>& parts) { return ParamText(parts[0]) + ", " + ParamText(parts[1]) + ", " + ParamText(parts[2]); });
	}

	std::string Vec2ParamText(const Vec2Param& a_param)
	{
		return Match(
			a_param,
			[](const Ref& r) { return "@" + r.name; },
			[](const std::array<Param, 2>& parts) { return ParamText(parts[0]) + ", " + ParamText(parts[1]); });
	}

	std::optional<Vec2Param> ParseVec2Param(std::string_view a_text)
	{
		const auto text = TrimCopy(a_text);
		if (text.starts_with('@') && text.size() > 1 && text.find(',') == std::string::npos) {
			return Vec2Param{ Ref{ text.substr(1) } };
		}
		const auto parts = SplitCommas(text);
		if (parts.size() == 1) {
			const auto single = ParseParam(parts[0]);
			if (single && Get<float>(*single)) {
				return Vec2Param{ std::array<Param, 2>{ *single, *single } };
			}
			return std::nullopt;
		}
		if (parts.size() != 2) {
			return std::nullopt;
		}
		std::array<Param, 2> out;
		for (std::size_t i = 0; i < 2; ++i) {
			const auto part = ParseParam(parts[i]);
			if (!part) {
				return std::nullopt;
			}
			out[i] = *part;
		}
		return Vec2Param{ out };
	}

	std::optional<Vec3Param> ParseVec3Param(std::string_view a_text)
	{
		const auto text = TrimCopy(a_text);
		if (text.starts_with('@') && text.size() > 1 && text.find(',') == std::string::npos) {
			return Vec3Param{ Ref{ text.substr(1) } };
		}
		const auto parts = SplitCommas(text);
		if (parts.size() == 1) {
			const auto single = ParseParam(parts[0]);
			if (single && Get<float>(*single)) {
				return Vec3Param{ std::array<Param, 3>{ *single, *single, *single } };
			}
			return std::nullopt;
		}
		if (parts.size() != 3) {
			return std::nullopt;
		}
		std::array<Param, 3> out;
		for (std::size_t i = 0; i < 3; ++i) {
			const auto part = ParseParam(parts[i]);
			if (!part) {
				return std::nullopt;
			}
			out[i] = *part;
		}
		return Vec3Param{ out };
	}

	void NormaliseColor(std::array<Param, 3>& a_parts) noexcept
	{
		float largest = 0.0f;
		for (const auto& part : a_parts) {
			const auto* number = Get<float>(part);
			if (!number) {
				return;
			}
			largest = (std::max)(largest, *number);
		}
		if (largest > 1.0f) {
			for (auto& part : a_parts) {
				part = *Get<float>(part) / 255.0f;
			}
		}
	}

	std::optional<Vec3Param> ParseColorParam(std::string_view a_text)
	{
		auto param = ParseVec3Param(a_text);
		if (param) {
			if (auto* parts = Get<std::array<Param, 3>>(*param)) {
				NormaliseColor(*parts);
			}
		}
		return param;
	}

	std::string LayerSourceText(const LayerSource& a_source)
	{
		return Match(
			a_source,
			[](const Ref& r) { return "@" + r.name; },
			[](const Vec3& v) { return NumberText(v.x) + ", " + NumberText(v.y) + ", " + NumberText(v.z); });
	}

	std::optional<LayerSource> ParseLayerSource(std::string_view a_text)
	{
		const auto text = TrimCopy(a_text);
		if (text.starts_with('@') && text.size() > 1) {
			return LayerSource{ Ref{ text.substr(1) } };
		}
		const auto parts = SplitCommas(text);
		if (parts.size() == 1) {
			const auto single = ParseNumber(parts[0]);
			return single ? std::optional{ LayerSource{ Vec3{ *single, *single, *single } } } : std::nullopt;
		}
		if (parts.size() != 3) {
			return std::nullopt;
		}
		const auto x = ParseNumber(parts[0]), y = ParseNumber(parts[1]), z = ParseNumber(parts[2]);
		if (!x || !y || !z) {
			return std::nullopt;
		}
		std::array<Param, 3> colour{ *x, *y, *z };
		NormaliseColor(colour);
		return LayerSource{ Vec3{ *Get<float>(colour[0]), *Get<float>(colour[1]), *Get<float>(colour[2]) } };
	}

	ValueType SourceType(const Source& a_source) noexcept
	{
		return Match(
			a_source.kind,
			[](const ImageSource& s) { return ShaderChannelOf(s.channel) == ShaderChannel::kRgb ? ValueType::kVec3 : ValueType::kScalar; },
			[](const MaterialSource& s) { return MaterialChannelType(s.channel); },
			[](const BakeSource& s) { return Is<PositionBake>(s.bake) || Is<LocalPositionBake>(s.bake) ? ValueType::kVec3 : ValueType::kScalar; },
			[](const auto&) { return ValueType::kScalar; });
	}
}
