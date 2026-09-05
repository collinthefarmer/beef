// Recipe files: JSON text to records and back, in the shape of
// schema/recipe.schema.json. All JSON mechanics live here; the reader helper
// below turns field access into named, checked steps that report on the
// row they belong to.

#include "Recipe.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <cstdlib>
#include <format>
#include <unordered_set>

namespace WornEnchantmentPBR
{
	using json = nlohmann::ordered_json;

	namespace
	{
		// ------------------------------------------------------- name tables

		template <class E>
		struct Named
		{
			E                value;
			std::string_view name;
		};

		template <class E, std::size_t N>
		std::string_view NameOf(const Named<E> (&a_table)[N], E a_value)
		{
			for (const auto& e : a_table) {
				if (e.value == a_value) {
					return e.name;
				}
			}
			return "?";
		}

		template <class E, std::size_t N>
		std::optional<E> FromName(const Named<E> (&a_table)[N], std::string_view a_name)
		{
			for (const auto& e : a_table) {
				if (e.name == a_name) {
					return e.value;
				}
			}
			return std::nullopt;
		}

		template <class E, std::size_t N>
		std::string Choices(const Named<E> (&a_table)[N])
		{
			std::string out;
			for (const auto& e : a_table) {
				out += (out.empty() ? "" : ", ") + std::string{ e.name };
			}
			return out;
		}

		constexpr Named<KeyKind> kKeyKinds[]{ { KeyKind::kMagicEffect, "magicEffect" }, { KeyKind::kEnchantment, "enchantment" }, { KeyKind::kEffectShader, "effectShader" }, { KeyKind::kKeyword, "keyword" }, { KeyKind::kMaterial, "material" }, { KeyKind::kArmor, "armor" } };
		constexpr Named<SelectorKind> kSelectorKinds[]{ { SelectorKind::kAddon, "addon" }, { SelectorKind::kGeometry, "geometry" }, { SelectorKind::kTexture, "texture" } };
		constexpr Named<Waveform> kWaveforms[]{ { Waveform::kSine, "sine" }, { Waveform::kTriangle, "triangle" }, { Waveform::kSquare, "square" }, { Waveform::kSaw, "saw" } };
		constexpr Named<EfshField> kEfshFields[]{ { EfshField::kFillAlpha, "fillAlpha" }, { EfshField::kFillColor, "fillColor" }, { EfshField::kEdgeAlpha, "edgeAlpha" }, { EfshField::kEdgeColor, "edgeColor" }, { EfshField::kScroll, "scroll" } };
		constexpr Named<Measure> kMeasures[]{ { Measure::kCurrent, "current" }, { Measure::kBase, "base" }, { Measure::kPermanent, "permanent" }, { Measure::kTemporaryModifier, "temporaryModifier" }, { Measure::kDamage, "damage" }, { Measure::kMax, "max" } };
		constexpr Named<ActorStateKind> kActorStates[]{ { ActorStateKind::kInCombat, "inCombat" }, { ActorStateKind::kSneaking, "sneaking" }, { ActorStateKind::kWeaponDrawn, "weaponDrawn" }, { ActorStateKind::kHostileDistance, "hostileDistance" } };
		constexpr Named<EnchantmentField> kEnchantmentFields[]{ { EnchantmentField::kMagnitude, "magnitude" }, { EnchantmentField::kCost, "cost" } };
		constexpr Named<PayloadField> kPayloadFields[]{ { PayloadField::kValue, "value" }, { PayloadField::kPosition, "position" }, { PayloadField::kNormal, "normal" } };
		constexpr Named<ImageChannel> kImageChannels[]{ { ImageChannel::kRgb, "rgb" }, { ImageChannel::kR, "r" }, { ImageChannel::kG, "g" }, { ImageChannel::kB, "b" }, { ImageChannel::kA, "a" }, { ImageChannel::kLuma, "luma" } };
		constexpr Named<ImageSpace> kImageSpaces[]{ { ImageSpace::kTiled, "tiled" }, { ImageSpace::kMesh, "mesh" } };
		constexpr Named<MaterialChannel> kMaterialChannels[]{ { MaterialChannel::kDiffuseRgb, "diffuseRgb" }, { MaterialChannel::kDiffuseLuma, "diffuseLuma" }, { MaterialChannel::kNormalSlope, "normalSlope" }, { MaterialChannel::kRoughness, "roughness" }, { MaterialChannel::kMetallic, "metallic" }, { MaterialChannel::kOcclusion, "occlusion" }, { MaterialChannel::kReflectance, "reflectance" }, { MaterialChannel::kDisplacement, "displacement" }, { MaterialChannel::kRelief, "relief" } };
		constexpr Named<UvAxis> kUvAxes[]{ { UvAxis::kU, "u" }, { UvAxis::kV, "v" } };
		constexpr Named<RippleShape> kRippleShapes[]{ { RippleShape::kRing, "ring" }, { RippleShape::kDisc, "disc" } };
		constexpr Named<Surface> kSurfaces[]{ { Surface::kMaterial, "material" }, { Surface::kShell, "shell" } };
		constexpr Named<Slot> kSlots[]{ { Slot::kDiffuse, "diffuse" }, { Slot::kEmissive, "emissive" }, { Slot::kRmaos, "rmaos" }, { Slot::kNormal, "normal" }, { Slot::kHeight, "height" }, { Slot::kFuzz, "fuzz" }, { Slot::kGlint, "glint" }, { Slot::kCoat, "coat" }, { Slot::kSubsurface, "subsurface" } };
		constexpr Named<Blend> kBlends[]{ { Blend::kReplace, "replace" }, { Blend::kMultiply, "multiply" }, { Blend::kAdd, "add" }, { Blend::kSubtract, "subtract" }, { Blend::kScreen, "screen" }, { Blend::kLerp, "lerp" }, { Blend::kNormal, "normal" } };
	}

	std::string_view BlendName(Blend a_blend) noexcept
	{
		for (const auto& n : kBlends) {
			if (n.value == a_blend) {
				return n.name;
			}
		}
		return "replace";
	}

	std::optional<Blend> ParseBlend(std::string_view a_name) noexcept
	{
		for (const auto& n : kBlends) {
			if (n.name == a_name) {
				return n.value;
			}
		}
		return std::nullopt;
	}

	std::string_view MaterialChannelName(MaterialChannel a_channel) noexcept
	{
		for (const auto& n : kMaterialChannels) {
			if (n.value == a_channel) {
				return n.name;
			}
		}
		return "?";
	}

	std::string_view ImageChannelName(ImageChannel a_channel) noexcept
	{
		for (const auto& n : kImageChannels) {
			if (n.value == a_channel) {
				return n.name;
			}
		}
		return "?";
	}

	std::string DescribeSource(const SourceKind& a_kind)
	{
		return Match(
			a_kind,
			[](const ImageSource& s) {
				return std::format("image {} ({}, {}{}{})", s.path, ImageChannelName(s.channel), s.space == ImageSpace::kMesh ? "mesh" : "tiled",
					s.scroll ? ", scrolling" : "", s.tile ? ", tiled" : "");
			},
			[](const MaterialSource& s) { return std::format("material {}", MaterialChannelName(s.channel)); },
			[](const BakeSource& s) {
				return Match(
					s.bake,
					[](const PositionBake&) { return std::string{ "bake position (bind pose, -128..128 units per axis as 0..1)" }; },
					[](const LocalPositionBake&) { return std::string{ "bake localPosition (this geometry's bound as 0..1)" }; },
					[](const WorldUpBake&) { return std::string{ "bake worldUp (bind-pose normal)" }; },
					[](const PartitionBake& p) { return std::format("bake partition {}", p.slot); },
					[](const BoneWeightBake& b) { return std::format("bake boneWeight of {} bone(s)", b.bones.size()); });
			},
			[](const UvSource& s) { return std::format("uv {} (the coordinate as a ramp over the islands)", s.axis == UvAxis::kU ? "u" : "v"); },
			[](const DistanceSource& s) {
				return Match(
					s.from,
					[](const std::string& node) { return std::format("distance from node {} (bind pose, 0..256 units as 0..1)", node); },
					[](const Vec3& p) { return std::format("distance from ({:.0f}, {:.0f}, {:.0f}) (bind pose, 0..256 units as 0..1)", p.x, p.y, p.z); });
			},
			[](const RippleSource& s) { return std::format("ripple {} from @{}, speed {}, width {}, decay {}", s.shape == RippleShape::kDisc ? "disc" : "ring", s.trigger.name, ParamText(s.speed), ParamText(s.width), ParamText(s.decay)); });
	}

	namespace
	{
		constexpr Named<ShellMaterial> kShellMaterials[]{ { ShellMaterial::kPbrCopy, "pbrCopy" }, { ShellMaterial::kVanilla, "vanilla" } };
		constexpr Named<ShellBlend> kShellBlends[]{ { ShellBlend::kAdditive, "additive" }, { ShellBlend::kAlpha, "alpha" } };

		// ----------------------------------------------------------- numbers

		// Shortest decimal that reads back as the same float, as a JSON number.
		json Num(float a_value)
		{
			char       buffer[32];
			const auto r = std::to_chars(buffer, buffer + sizeof(buffer), a_value);
			return json(std::strtod(std::string(buffer, r.ptr).c_str(), nullptr));
		}

		// ------------------------------------------------------- diagnostics

		struct Ctx
		{
			std::vector<Diagnostic>& out;
			std::string              where;

			void Error(std::string a_message) const { out.push_back({ Severity::kError, where, std::move(a_message) }); }
			void Warn(std::string a_message) const { out.push_back({ Severity::kWarning, where, std::move(a_message) }); }
			Ctx  At(std::string a_where) const { return Ctx{ out, std::move(a_where) }; }
		};

		// --------------------------------------------------------- reading

		// One JSON object, read field by field; every field read is
		// remembered so Finish() can report the ones nobody asked for.
		class Reader
		{
		public:
			Reader(const json& a_object, Ctx a_ctx) :
				object_(a_object), ctx_(std::move(a_ctx)) {}

			[[nodiscard]] const Ctx& Context() const noexcept { return ctx_; }
			[[nodiscard]] bool       Has(std::string_view a_key) const { return object_.is_object() && object_.contains(a_key); }

			// The raw child, marked as read; null when absent.
			const json* Child(std::string_view a_key)
			{
				if (!Has(a_key)) {
					return nullptr;
				}
				used_.insert(std::string{ a_key });
				return &object_.at(a_key);
			}

			std::optional<float> Number(std::string_view a_key)
			{
				const auto* j = Child(a_key);
				if (!j) {
					return std::nullopt;
				}
				if (!j->is_number()) {
					ctx_.Error(std::format("'{}' must be a number", a_key));
					return std::nullopt;
				}
				return static_cast<float>(j->get<double>());
			}

			std::optional<int> Integer(std::string_view a_key)
			{
				const auto* j = Child(a_key);
				if (!j) {
					return std::nullopt;
				}
				if (!j->is_number_integer()) {
					ctx_.Error(std::format("'{}' must be an integer", a_key));
					return std::nullopt;
				}
				return j->get<int>();
			}

			std::optional<bool> Boolean(std::string_view a_key)
			{
				const auto* j = Child(a_key);
				if (!j) {
					return std::nullopt;
				}
				if (!j->is_boolean()) {
					ctx_.Error(std::format("'{}' must be true or false", a_key));
					return std::nullopt;
				}
				return j->get<bool>();
			}

			std::optional<std::string> String(std::string_view a_key)
			{
				const auto* j = Child(a_key);
				if (!j) {
					return std::nullopt;
				}
				if (!j->is_string()) {
					ctx_.Error(std::format("'{}' must be a string", a_key));
					return std::nullopt;
				}
				return j->get<std::string>();
			}

			std::string Required(std::string_view a_key)
			{
				if (!Has(a_key)) {
					ctx_.Error(std::format("'{}' is required", a_key));
					return {};
				}
				return String(a_key).value_or(std::string{});
			}

			template <class E, std::size_t N>
			std::optional<E> Enum(std::string_view a_key, const Named<E> (&a_table)[N])
			{
				const auto text = String(a_key);
				if (!text) {
					return std::nullopt;
				}
				const auto value = FromName(a_table, *text);
				if (!value) {
					ctx_.Error(std::format("'{}' is not one of {}: '{}'", a_key, Choices(a_table), *text));
				}
				return value;
			}

			std::optional<Ref> Reference(std::string_view a_key)
			{
				const auto* j = Child(a_key);
				return j ? RefFrom(*j, a_key) : std::nullopt;
			}

			std::optional<Param> Parameter(std::string_view a_key)
			{
				const auto* j = Child(a_key);
				return j ? ParamFrom(*j, a_key) : std::nullopt;
			}

			std::optional<Vec2Param> Vector2(std::string_view a_key)
			{
				const auto* j = Child(a_key);
				return j ? VecFrom<2>(*j, a_key, false) : std::nullopt;
			}

			std::optional<Vec3Param> Vector3(std::string_view a_key, bool a_color = false)
			{
				const auto* j = Child(a_key);
				return j ? VecFrom<3>(*j, a_key, a_color) : std::nullopt;
			}

			// A point in space: three numbers, never a colour.
			static std::optional<Vec3> PointFrom(const json& a_j, std::string_view a_what, const Ctx& a_ctx)
			{
				if (!a_j.is_array() || a_j.size() != 3 || !std::ranges::all_of(a_j, [](const json& e) { return e.is_number(); })) {
					a_ctx.Error(std::format("'{}' must be [x, y, z]", a_what));
					return std::nullopt;
				}
				return Vec3{ a_j[0].get<float>(), a_j[1].get<float>(), a_j[2].get<float>() };
			}

			std::optional<Vec3> Point(std::string_view a_key)
			{
				const auto* j = Child(a_key);
				return j ? PointFrom(*j, a_key, ctx_) : std::nullopt;
			}

			std::optional<Value> Literal(std::string_view a_key)
			{
				const auto* j = Child(a_key);
				return j ? ValueFrom(*j, a_key, ctx_) : std::nullopt;
			}

			// Reports every key that no reader asked for.
			void Finish()
			{
				if (!object_.is_object()) {
					return;
				}
				for (const auto& [key, value] : object_.items()) {
					if (!used_.contains(key)) {
						ctx_.Error(std::format("unknown key '{}'", key));
					}
				}
			}

			// ----- value forms, usable on any json

			std::optional<Ref> RefFrom(const json& a_j, std::string_view a_what) const
			{
				if (a_j.is_string()) {
					const auto text = a_j.get<std::string>();
					if (text.size() > 1 && text[0] == '@') {
						return Ref{ text.substr(1) };
					}
					ctx_.Error(std::format("'{}' names a row and must start with '@': '{}'", a_what, text));
					return std::nullopt;
				}
				ctx_.Error(std::format("'{}' must be \"@name\"", a_what));
				return std::nullopt;
			}

			std::optional<Param> ParamFrom(const json& a_j, std::string_view a_what) const
			{
				if (a_j.is_number()) {
					return Param{ static_cast<float>(a_j.get<double>()) };
				}
				if (a_j.is_string()) {
					const auto ref = RefFrom(a_j, a_what);
					return ref ? std::optional<Param>{ *ref } : std::nullopt;
				}
				ctx_.Error(std::format("'{}' must be a number or \"@name\"", a_what));
				return std::nullopt;
			}

			template <std::size_t N>
			std::optional<std::variant<std::array<Param, N>, Ref>> VecFrom(const json& a_j, std::string_view a_what, bool a_color) const
			{
				using V = std::variant<std::array<Param, N>, Ref>;
				if (a_j.is_string()) {
					const auto ref = RefFrom(a_j, a_what);
					return ref ? std::optional<V>{ *ref } : std::nullopt;
				}
				if (!a_j.is_array() || a_j.size() != N) {
					ctx_.Error(std::format("'{}' must be an array of {} numbers or \"@name\"s, or one \"@name\"", a_what, N));
					return std::nullopt;
				}
				std::array<Param, N> parts;
				bool                 allNumbers = true;
				float                largest = 0.0f;
				for (std::size_t i = 0; i < N; ++i) {
					const auto p = ParamFrom(a_j[i], a_what);
					if (!p) {
						return std::nullopt;
					}
					parts[i] = *p;
					if (const auto* f = Get<float>(*p)) {
						largest = std::max(largest, *f);
					} else {
						allNumbers = false;
					}
				}
				// Light Placer's rule: a component above 1 means a 0..255 colour.
				if (a_color && allNumbers && largest > 1.0f) {
					for (auto& p : parts) {
						p = std::get<float>(p) / 255.0f;
					}
				}
				return V{ parts };
			}

			static std::optional<Value> ValueFrom(const json& a_j, std::string_view a_what, const Ctx& a_ctx)
			{
				if (a_j.is_number()) {
					return Value{ static_cast<float>(a_j.get<double>()) };
				}
				if (a_j.is_array() && (a_j.size() == 2 || a_j.size() == 3) && std::ranges::all_of(a_j, [](const json& e) { return e.is_number(); })) {
					if (a_j.size() == 2) {
						return Value{ Vec2{ a_j[0].get<float>(), a_j[1].get<float>() } };
					}
					Vec3 v{ a_j[0].get<float>(), a_j[1].get<float>(), a_j[2].get<float>() };
					if (v.x > 1.0f || v.y > 1.0f || v.z > 1.0f) {
						v = Vec3{ v.x / 255.0f, v.y / 255.0f, v.z / 255.0f };
					}
					return Value{ v };
				}
				a_ctx.Error(std::format("'{}' must be a number, [x, y] or [r, g, b]", a_what));
				return std::nullopt;
			}

		private:
			const json&                     object_;
			Ctx                             ctx_;
			std::unordered_set<std::string> used_;
		};

		// A one-key object: its kind key and the value under it.
		struct KindEntry
		{
			std::string key;
			const json* value = nullptr;
		};

		std::optional<KindEntry> OneKey(const json& a_j, const Ctx& a_ctx, std::string_view a_what, std::initializer_list<std::string_view> a_common = {})
		{
			if (!a_j.is_object()) {
				a_ctx.Error(std::format("{} must be an object with one kind key", a_what));
				return std::nullopt;
			}
			std::optional<KindEntry> found;
			for (const auto& [key, value] : a_j.items()) {
				if (std::ranges::find(a_common, key) != a_common.end()) {
					continue;
				}
				if (found) {
					a_ctx.Error(std::format("{} has two kind keys, '{}' and '{}'", a_what, found->key, key));
					return std::nullopt;
				}
				found = KindEntry{ key, &value };
			}
			if (!found) {
				a_ctx.Error(std::format("{} has no kind key", a_what));
			}
			return found;
		}

		// ----------------------------------------------------------- forms

		std::optional<FormRef> FormFrom(const json& a_j, const Ctx& a_ctx, std::string_view a_what)
		{
			if (!a_j.is_string() || a_j.get<std::string>().empty()) {
				a_ctx.Error(std::format("'{}' must be an editor ID or \"0x<id>~<plugin>\"", a_what));
				return std::nullopt;
			}
			return FormRef::From(a_j.get<std::string>());
		}

		std::optional<std::string> GlobFrom(const json& a_j, const Ctx& a_ctx, std::string_view a_what)
		{
			if (!a_j.is_string() || a_j.get<std::string>().empty()) {
				a_ctx.Error(std::format("'{}' must be a glob string", a_what));
				return std::nullopt;
			}
			return a_j.get<std::string>();
		}

		// ------------------------------------------------------------- keys

		std::optional<RecipeKey> KeyFrom(const json& a_j, const Ctx& a_ctx)
		{
			if (a_j.is_string()) {
				if (a_j.get<std::string>() == "default") {
					return RecipeKey{ KeyKind::kDefault, {}, {} };
				}
				a_ctx.Error(std::format("a key is \"default\" or {{\"<kind>\": ...}}; got '{}'", a_j.get<std::string>()));
				return std::nullopt;
			}
			const auto entry = OneKey(a_j, a_ctx, "a key");
			if (!entry) {
				return std::nullopt;
			}
			const auto kind = FromName(kKeyKinds, entry->key);
			if (!kind) {
				a_ctx.Error(std::format("unknown key kind '{}'; one of {}", entry->key, Choices(kKeyKinds)));
				return std::nullopt;
			}
			RecipeKey key;
			key.kind = *kind;
			if (*kind == KeyKind::kMaterial) {
				const auto glob = GlobFrom(*entry->value, a_ctx, entry->key);
				if (!glob) {
					return std::nullopt;
				}
				key.glob = *glob;
				return key;
			}
			const auto form = FormFrom(*entry->value, a_ctx, entry->key);
			if (!form) {
				return std::nullopt;
			}
			key.form = *form;
			return key;
		}

		json KeyToJson(const RecipeKey& a_key)
		{
			switch (a_key.kind) {
			case KeyKind::kDefault:
				return json("default");
			case KeyKind::kMaterial:
				return json::object({ { "material", a_key.glob } });
			default:
				return json::object({ { std::string{ NameOf(kKeyKinds, a_key.kind) }, a_key.form.text } });
			}
		}

		// -------------------------------------------------------- selectors

		Selector SelectorFrom(const json& a_j, const Ctx& a_ctx)
		{
			Selector s;
			if (!a_j.is_array()) {
				a_ctx.Error("'selector' must be an array of {\"kind\": ...} terms");
				return s;
			}
			for (const auto& e : a_j) {
				const auto entry = OneKey(e, a_ctx, "a selector term");
				if (!entry) {
					continue;
				}
				const auto kind = FromName(kSelectorKinds, entry->key);
				if (!kind) {
					a_ctx.Error(std::format("unknown selector kind '{}'; one of {}", entry->key, Choices(kSelectorKinds)));
					continue;
				}
				SelectorTerm term;
				term.kind = *kind;
				if (*kind == SelectorKind::kAddon) {
					const auto form = FormFrom(*entry->value, a_ctx, entry->key);
					if (!form) {
						continue;
					}
					term.form = *form;
				} else {
					const auto glob = GlobFrom(*entry->value, a_ctx, entry->key);
					if (!glob) {
						continue;
					}
					term.glob = *glob;
				}
				s.anyOf.push_back(std::move(term));
			}
			return s;
		}

		json SelectorToJson(const Selector& a_selector)
		{
			json out = json::array();
			for (const auto& t : a_selector.anyOf) {
				out.push_back(json::object({ { std::string{ NameOf(kSelectorKinds, t.kind) }, t.kind == SelectorKind::kAddon ? t.form.text : t.glob } }));
			}
			return out;
		}

		// ------------------------------------------------------- parameters

		json ParamToJson(const Param& a_param)
		{
			return Match(
				a_param,
				[](float f) { return Num(f); },
				[](const Ref& r) { return json("@" + r.name); });
		}

		template <std::size_t N>
		json VecToJson(const std::variant<std::array<Param, N>, Ref>& a_param)
		{
			return Match(
				a_param,
				[](const Ref& r) { return json("@" + r.name); },
				[](const std::array<Param, N>& parts) {
					json out = json::array();
					for (const auto& p : parts) {
						out.push_back(ParamToJson(p));
					}
					return out;
				});
		}

		json ValueToJson(const Value& a_value)
		{
			return Match(
				a_value,
				[](float f) { return Num(f); },
				[](const Vec2& v) { return json::array({ Num(v.x), Num(v.y) }); },
				[](const Vec3& v) { return json::array({ Num(v.x), Num(v.y), Num(v.z) }); });
		}

		json PointToJson(const Vec3& a_v)
		{
			return json::array({ Num(a_v.x), Num(a_v.y), Num(a_v.z) });
		}

		json CurveRefToJson(const CurveRef& a_curve)
		{
			return json(a_curve.text);
		}

		std::optional<CurveRef> CurveRefFrom(Reader& a_reader)
		{
			const auto text = a_reader.String("curve");
			if (!text) {
				return std::nullopt;
			}
			if (text->empty()) {
				a_reader.Context().Error("'curve' is empty");
				return std::nullopt;
			}
			return CurveRef{ *text };
		}

		// ---------------------------------------------------------- signals

		std::optional<Signal> SignalFrom(const std::string& a_name, const json& a_j, const Ctx& a_ctx)
		{
			const auto entry = OneKey(a_j, a_ctx, "a signal", { "curve" });
			if (!entry) {
				return std::nullopt;
			}
			Signal      s;
			s.name = a_name;
			Reader      row(a_j, a_ctx);
			const auto& kind = entry->key;
			const json& v = *entry->value;
			row.Child(kind);
			const auto object = [&](auto a_fill) -> bool {
				if (!v.is_object()) {
					a_ctx.Error(std::format("'{}' takes an object", kind));
					return false;
				}
				Reader inner(v, a_ctx);
				a_fill(inner);
				inner.Finish();
				return true;
			};

			if (kind == "constant") {
				const auto value = Reader::ValueFrom(v, kind, a_ctx);
				if (!value) {
					return std::nullopt;
				}
				s.kind = ConstantSignal{ *value };
			} else if (kind == "pulse") {
				PulseSignal k;
				if (!object([&](Reader& r) {
						if (auto p = r.Parameter("base")) k.base = *p;
						if (auto p = r.Parameter("amplitude")) k.amplitude = *p;
						if (auto p = r.Parameter("period")) k.period = *p;
						if (auto p = r.Parameter("phase")) k.phase = *p;
						if (auto w = r.Enum("waveform", kWaveforms)) k.waveform = *w;
					})) {
					return std::nullopt;
				}
				s.kind = k;
			} else if (kind == "ramp") {
				RampSignal k;
				if (!object([&](Reader& r) {
						if (auto p = r.Parameter("from")) k.from = *p;
						if (auto p = r.Parameter("to")) k.to = *p;
						if (auto p = r.Parameter("seconds")) k.seconds = *p;
					})) {
					return std::nullopt;
				}
				s.kind = k;
			} else if (kind == "efsh") {
				EfshSignal k;
				if (!object([&](Reader& r) {
						if (auto f = r.Enum("field", kEfshFields)) k.field = *f;
						else if (!r.Has("field")) a_ctx.Error("'efsh' needs 'field'");
						if (const auto* rec = r.Child("record")) {
							if (auto form = FormFrom(*rec, a_ctx, "record")) k.record = *form;
						} else {
							a_ctx.Error("'efsh' needs 'record'");
						}
					})) {
					return std::nullopt;
				}
				s.kind = k;
			} else if (kind == "av") {
				ActorValueSignal k;
				if (v.is_string()) {
					k.actorValue = v.get<std::string>();
				} else if (!object([&](Reader& r) {
							   k.actorValue = r.Required("of");
							   if (auto m = r.Enum("measure", kMeasures)) k.measure = *m;
						   })) {
					return std::nullopt;
				}
				if (k.actorValue.empty()) {
					a_ctx.Error("'av' needs an actor value name");
				}
				s.kind = k;
			} else if (kind == "actorState") {
				const auto state = v.is_string() ? FromName(kActorStates, v.get<std::string>()) : std::nullopt;
				if (!state) {
					a_ctx.Error(std::format("'actorState' is one of {}", Choices(kActorStates)));
					return std::nullopt;
				}
				s.kind = ActorStateSignal{ *state };
			} else if (kind == "enchantment") {
				const auto field = v.is_string() ? FromName(kEnchantmentFields, v.get<std::string>()) : std::nullopt;
				if (!field) {
					a_ctx.Error(std::format("'enchantment' is one of {}", Choices(kEnchantmentFields)));
					return std::nullopt;
				}
				s.kind = EnchantmentSignal{ *field };
			} else if (kind == "trigger") {
				TriggerSignal k;
				if (!v.is_object()) {
					a_ctx.Error("'trigger' takes an object");
					return std::nullopt;
				}
				const auto source = OneKey(v, a_ctx, "a trigger", { "lifetime", "max", "filter", "at", "value" });
				if (!source) {
					return std::nullopt;
				}
				Reader r(v, a_ctx);
				if (auto p = r.Parameter("lifetime")) k.lifetime = *p;
				if (auto m = r.Integer("max")) {
					if (*m < 1) {
						a_ctx.Error("'max' must be at least 1");
					} else {
						k.max = static_cast<std::uint32_t>(*m);
					}
				}
				r.Child(source->key);
				if (source->key == "event") {
					EventSource es;
					if (!source->value->is_string() || source->value->get<std::string>().empty()) {
						a_ctx.Error("'event' is an id glob string");
						return std::nullopt;
					}
					es.event = source->value->get<std::string>();
					if (auto at = r.String("at")) es.at = *at;
					if (const auto* f = r.Child("filter")) {
						Reader fr(*f, a_ctx);
						if (auto n = fr.String("node")) es.filter.node = *n;
						if (auto arg = fr.String("arg")) es.filter.arg = *arg;
						if (const auto* range = fr.Child("value")) {
							if (!range->is_array() || range->size() != 2) {
								a_ctx.Error("'filter.value' is [min, max], either may be null");
							} else {
								if ((*range)[0].is_number()) es.filter.value.min = (*range)[0].get<float>();
								if ((*range)[1].is_number()) es.filter.value.max = (*range)[1].get<float>();
							}
						}
						fr.Finish();
					}
					if (r.Has("value")) a_ctx.Error("'value' belongs to a 'when' trigger");
					k.source = es;
				} else if (source->key == "plugin") {
					if (!source->value->is_string() || source->value->get<std::string>().empty()) {
						a_ctx.Error("'plugin' is an id string");
						return std::nullopt;
					}
					if (r.Has("filter") || r.Has("at") || r.Has("value")) a_ctx.Error("'filter', 'at' and 'value' belong to 'event' or 'when' triggers");
					k.source = PluginSource{ source->value->get<std::string>() };
				} else if (source->key == "when") {
					WhenSource ws;
					const auto when = r.RefFrom(*source->value, "when");
					if (!when) {
						return std::nullopt;
					}
					ws.when = *when;
					ws.value = r.Reference("value");
					if (r.Has("filter") || r.Has("at")) a_ctx.Error("'filter' and 'at' belong to 'event' triggers");
					k.source = ws;
				} else {
					a_ctx.Error(std::format("a trigger's source is 'event', 'plugin' or 'when', not '{}'", source->key));
					return std::nullopt;
				}
				r.Finish();
				s.kind = k;
			} else if (kind == "payload") {
				PayloadSignal k;
				if (!object([&](Reader& r) {
						if (auto t = r.Reference("trigger")) k.trigger = *t;
						else if (!r.Has("trigger")) a_ctx.Error("'payload' needs 'trigger'");
						if (auto f = r.Enum("field", kPayloadFields)) k.field = *f;
						else if (!r.Has("field")) a_ctx.Error("'payload' needs 'field'");
					})) {
					return std::nullopt;
				}
				s.kind = k;
			} else if (kind == "counter") {
				CounterSignal k;
				if (!object([&](Reader& r) {
						if (auto t = r.Reference("trigger")) k.trigger = *t;
						else if (!r.Has("trigger")) a_ctx.Error("'counter' needs 'trigger'");
						k.reset = r.Reference("reset");
						k.cap = r.Parameter("cap");
					})) {
					return std::nullopt;
				}
				s.kind = k;
			} else if (kind == "accumulate") {
				AccumulateSignal k;
				if (!object([&](Reader& r) {
						if (auto t = r.Reference("trigger")) k.trigger = *t;
						else if (!r.Has("trigger")) a_ctx.Error("'accumulate' needs 'trigger'");
						if (auto p = r.Parameter("decay")) k.decay = *p;
					})) {
					return std::nullopt;
				}
				s.kind = k;
			} else if (kind == "noise") {
				NoiseSignal k;
				if (!object([&](Reader& r) {
						if (auto p = r.Parameter("frequency")) k.frequency = *p;
						if (auto p = r.Parameter("amplitude")) k.amplitude = *p;
						if (auto seed = r.Integer("seed")) k.seed = static_cast<std::uint32_t>(std::max(0, *seed));
					})) {
					return std::nullopt;
				}
				s.kind = k;
			} else if (kind == "gradient") {
				GradientSignal k;
				if (!object([&](Reader& r) {
						if (auto p = r.Parameter("t")) k.t = *p;
						else if (!r.Has("t")) a_ctx.Error("'gradient' needs 't'");
						const auto* stops = r.Child("stops");
						if (!stops || !stops->is_array() || stops->empty()) {
							a_ctx.Error("'gradient' needs a non-empty 'stops' array");
							return;
						}
						for (const auto& stop : *stops) {
							GradientStop gs;
							Reader       sr(stop, a_ctx);
							if (auto at = sr.Number("at")) gs.at = *at;
							else if (!sr.Has("at")) a_ctx.Error("a stop needs 'at'");
							if (auto c = sr.Vector3("color", true)) gs.color = *c;
							else if (!sr.Has("color")) a_ctx.Error("a stop needs 'color'");
							sr.Finish();
							k.stops.push_back(gs);
						}
					})) {
					return std::nullopt;
				}
				s.kind = k;
			} else if (kind == "delta") {
				const auto of = row.RefFrom(v, kind);
				if (!of) {
					return std::nullopt;
				}
				s.kind = DeltaSignal{ *of };
			} else if (kind == "smooth") {
				SmoothSignal k;
				if (!object([&](Reader& r) {
						if (auto of = r.Reference("of")) k.of = *of;
						else if (!r.Has("of")) a_ctx.Error("'smooth' needs 'of'");
						if (auto p = r.Parameter("seconds")) k.seconds = *p;
					})) {
					return std::nullopt;
				}
				s.kind = k;
			} else if (kind == "expr") {
				if (!v.is_string() || v.get<std::string>().empty()) {
					a_ctx.Error("'expr' is an expression string");
					return std::nullopt;
				}
				s.kind = ExprSignal{ v.get<std::string>() };
			} else {
				a_ctx.Error(std::format("unknown signal kind '{}'", kind));
				return std::nullopt;
			}
			s.curve = CurveRefFrom(row);
			row.Finish();
			return s;
		}

		json SignalToJson(const Signal& a_signal)
		{
			json row = json::object();
			Match(
				a_signal.kind,
				[&](const ConstantSignal& k) { row["constant"] = ValueToJson(k.value); },
				[&](const PulseSignal& k) {
					json o = json::object();
					o["base"] = ParamToJson(k.base);
					o["amplitude"] = ParamToJson(k.amplitude);
					o["period"] = ParamToJson(k.period);
					if (k.phase != Param{ 0.0f }) o["phase"] = ParamToJson(k.phase);
					if (k.waveform != Waveform::kSine) o["waveform"] = NameOf(kWaveforms, k.waveform);
					row["pulse"] = std::move(o);
				},
				[&](const RampSignal& k) { row["ramp"] = json::object({ { "from", ParamToJson(k.from) }, { "to", ParamToJson(k.to) }, { "seconds", ParamToJson(k.seconds) } }); },
				[&](const EfshSignal& k) { row["efsh"] = json::object({ { "field", NameOf(kEfshFields, k.field) }, { "record", k.record.text } }); },
				[&](const ActorValueSignal& k) {
					if (k.measure == Measure::kCurrent) {
						row["av"] = k.actorValue;
					} else {
						row["av"] = json::object({ { "of", k.actorValue }, { "measure", NameOf(kMeasures, k.measure) } });
					}
				},
				[&](const ActorStateSignal& k) { row["actorState"] = NameOf(kActorStates, k.kind); },
				[&](const EnchantmentSignal& k) { row["enchantment"] = NameOf(kEnchantmentFields, k.field); },
				[&](const TriggerSignal& k) {
					json o = json::object();
					Match(
						k.source,
						[&](const EventSource& e) {
							o["event"] = e.event;
							if (e.filter != EventFilter{}) {
								json f = json::object();
								if (!e.filter.node.empty()) f["node"] = e.filter.node;
								if (!e.filter.arg.empty()) f["arg"] = e.filter.arg;
								if (e.filter.value != ValueRange{}) {
									f["value"] = json::array({ e.filter.value.min ? Num(*e.filter.value.min) : json(nullptr), e.filter.value.max ? Num(*e.filter.value.max) : json(nullptr) });
								}
								o["filter"] = std::move(f);
							}
							if (!e.at.empty()) o["at"] = e.at;
						},
						[&](const PluginSource& p) { o["plugin"] = p.id; },
						[&](const WhenSource& w) {
							o["when"] = "@" + w.when.name;
							if (w.value) o["value"] = "@" + w.value->name;
						});
					o["lifetime"] = ParamToJson(k.lifetime);
					o["max"] = k.max;
					row["trigger"] = std::move(o);
				},
				[&](const PayloadSignal& k) { row["payload"] = json::object({ { "trigger", "@" + k.trigger.name }, { "field", NameOf(kPayloadFields, k.field) } }); },
				[&](const CounterSignal& k) {
					json o = json::object({ { "trigger", "@" + k.trigger.name } });
					if (k.reset) o["reset"] = "@" + k.reset->name;
					if (k.cap) o["cap"] = ParamToJson(*k.cap);
					row["counter"] = std::move(o);
				},
				[&](const AccumulateSignal& k) { row["accumulate"] = json::object({ { "trigger", "@" + k.trigger.name }, { "decay", ParamToJson(k.decay) } }); },
				[&](const NoiseSignal& k) {
					json o = json::object({ { "frequency", ParamToJson(k.frequency) }, { "amplitude", ParamToJson(k.amplitude) } });
					if (k.seed != 0) o["seed"] = k.seed;
					row["noise"] = std::move(o);
				},
				[&](const GradientSignal& k) {
					json stops = json::array();
					for (const auto& s : k.stops) {
						stops.push_back(json::object({ { "at", Num(s.at) }, { "color", VecToJson(s.color) } }));
					}
					row["gradient"] = json::object({ { "t", ParamToJson(k.t) }, { "stops", std::move(stops) } });
				},
				[&](const DeltaSignal& k) { row["delta"] = "@" + k.of.name; },
				[&](const SmoothSignal& k) { row["smooth"] = json::object({ { "of", "@" + k.of.name }, { "seconds", ParamToJson(k.seconds) } }); },
				[&](const ExprSignal& k) { row["expr"] = k.text; });
			if (a_signal.curve) {
				row["curve"] = CurveRefToJson(*a_signal.curve);
			}
			return row;
		}

		// ---------------------------------------------------------- sources

		std::optional<Source> SourceFrom(const std::string& a_name, const json& a_j, const Ctx& a_ctx)
		{
			const auto entry = OneKey(a_j, a_ctx, "a source");
			if (!entry) {
				return std::nullopt;
			}
			Source      s;
			s.name = a_name;
			const auto& kind = entry->key;
			const json& v = *entry->value;
			if (kind == "image") {
				if (!v.is_object()) {
					a_ctx.Error("'image' takes an object with 'path'");
					return std::nullopt;
				}
				ImageSource k;
				Reader      r(v, a_ctx);
				k.path = r.Required("path");
				if (auto c = r.Enum("channel", kImageChannels)) k.channel = *c;
				if (auto sp = r.Enum("space", kImageSpaces)) k.space = *sp;
				k.scroll = r.Vector2("scroll");
				k.tile = r.Vector2("tile");
				if (const auto* m = r.Child("mirror")) {
					if (!m->is_array() || m->size() != 2 || !(*m)[0].is_boolean() || !(*m)[1].is_boolean()) {
						a_ctx.Error("'mirror' is [u, v] booleans");
					} else {
						k.mirror = { (*m)[0].get<bool>(), (*m)[1].get<bool>() };
					}
				}
				if (auto t = r.Boolean("transpose")) k.transpose = *t;
				if (auto mip = r.Number("mip")) k.mip = std::max(0.0f, *mip);
				r.Finish();
				s.kind = k;
			} else if (kind == "material") {
				const auto channel = v.is_string() ? FromName(kMaterialChannels, v.get<std::string>()) : std::nullopt;
				if (!channel) {
					a_ctx.Error(std::format("'material' is one of {}", Choices(kMaterialChannels)));
					return std::nullopt;
				}
				s.kind = MaterialSource{ *channel };
			} else if (kind == "bake") {
				BakeSource k;
				if (v.is_string()) {
					const auto text = v.get<std::string>();
					if (text == "position") {
						k.bake = PositionBake{};
					} else if (text == "localPosition") {
						k.bake = LocalPositionBake{};
					} else if (text == "worldUp") {
						k.bake = WorldUpBake{};
					} else {
						a_ctx.Error("'bake' is \"position\", \"localPosition\", \"worldUp\", {\"partition\": slot} or {\"boneWeight\": [bones]}");
						return std::nullopt;
					}
				} else {
					const auto inner = OneKey(v, a_ctx, "'bake'");
					if (!inner) {
						return std::nullopt;
					}
					if (inner->key == "partition") {
						PartitionBake pb;
						if (inner->value->is_string()) {
							const auto slot = BipedSlotFromName(inner->value->get<std::string>());
							if (!slot) {
								a_ctx.Error(std::format("unknown biped slot name '{}'", inner->value->get<std::string>()));
								return std::nullopt;
							}
							pb.slot = *slot;
						} else if (inner->value->is_number_integer() && inner->value->get<int>() >= 30 && inner->value->get<int>() <= 61) {
							pb.slot = inner->value->get<std::uint32_t>();
						} else {
							a_ctx.Error("'partition' is a biped slot name or a number 30..61");
							return std::nullopt;
						}
						k.bake = pb;
					} else if (inner->key == "boneWeight") {
						BoneWeightBake bw;
						if (!inner->value->is_array()) {
							a_ctx.Error("'boneWeight' is an array of bone names");
							return std::nullopt;
						}
						for (const auto& b : *inner->value) {
							if (b.is_string()) {
								bw.bones.push_back(b.get<std::string>());
							} else {
								a_ctx.Error("'boneWeight' entries are bone names");
							}
						}
						k.bake = bw;
					} else {
						a_ctx.Error(std::format("unknown bake '{}'", inner->key));
						return std::nullopt;
					}
				}
				s.kind = k;
			} else if (kind == "uv") {
				const auto axis = v.is_string() ? FromName(kUvAxes, v.get<std::string>()) : std::nullopt;
				if (!axis) {
					a_ctx.Error("'uv' is \"u\" or \"v\"");
					return std::nullopt;
				}
				s.kind = UvSource{ *axis };
			} else if (kind == "distance") {
				DistanceSource k;
				if (v.is_string()) {
					k.from = v.get<std::string>();
				} else if (v.is_object()) {
					Reader r(v, a_ctx);
					if (const auto* from = r.Child("from")) {
						if (from->is_string()) {
							k.from = from->get<std::string>();
						} else if (auto point = Reader::PointFrom(*from, "from", a_ctx)) {
							k.from = *point;
						}
					} else {
						a_ctx.Error("'distance' needs 'from'");
					}
					r.Finish();
				} else {
					a_ctx.Error("'distance' is a node name or {\"from\": ...}");
					return std::nullopt;
				}
				s.kind = k;
			} else if (kind == "ripple") {
				if (!v.is_object()) {
					a_ctx.Error("'ripple' takes an object with 'trigger'");
					return std::nullopt;
				}
				RippleSource k;
				Reader       r(v, a_ctx);
				if (auto t = r.Reference("trigger")) k.trigger = *t;
				else if (!r.Has("trigger")) a_ctx.Error("'ripple' needs 'trigger'");
				if (auto p = r.Parameter("speed")) k.speed = *p;
				if (auto p = r.Parameter("width")) k.width = *p;
				if (auto p = r.Parameter("decay")) k.decay = *p;
				if (auto sh = r.Enum("shape", kRippleShapes)) k.shape = *sh;
				r.Finish();
				s.kind = k;
			} else {
				a_ctx.Error(std::format("unknown source kind '{}'", kind));
				return std::nullopt;
			}
			return s;
		}

		json SourceToJson(const Source& a_source)
		{
			json row = json::object();
			Match(
				a_source.kind,
				[&](const ImageSource& k) {
					json o = json::object({ { "path", k.path } });
					if (k.channel != ImageChannel::kRgb) o["channel"] = NameOf(kImageChannels, k.channel);
					if (k.space != ImageSpace::kTiled) o["space"] = NameOf(kImageSpaces, k.space);
					if (k.scroll) o["scroll"] = VecToJson(*k.scroll);
					if (k.tile) o["tile"] = VecToJson(*k.tile);
					if (k.mirror[0] || k.mirror[1]) o["mirror"] = json::array({ k.mirror[0], k.mirror[1] });
					if (k.transpose) o["transpose"] = true;
					if (k.mip != 0.0f) o["mip"] = Num(k.mip);
					row["image"] = std::move(o);
				},
				[&](const MaterialSource& k) { row["material"] = NameOf(kMaterialChannels, k.channel); },
				[&](const BakeSource& k) {
					Match(
						k.bake,
						[&](const PositionBake&) { row["bake"] = "position"; },
						[&](const LocalPositionBake&) { row["bake"] = "localPosition"; },
						[&](const WorldUpBake&) { row["bake"] = "worldUp"; },
						[&](const PartitionBake& p) {
							const auto name = BipedSlotName(p.slot);
							row["bake"] = json::object({ { "partition", name ? json(std::string{ *name }) : json(p.slot) } });
						},
						[&](const BoneWeightBake& b) { row["bake"] = json::object({ { "boneWeight", b.bones } }); });
				},
				[&](const UvSource& k) { row["uv"] = NameOf(kUvAxes, k.axis); },
				[&](const DistanceSource& k) {
					Match(
						k.from,
						[&](const std::string& node) { row["distance"] = node; },
						[&](const Vec3& p) { row["distance"] = json::object({ { "from", PointToJson(p) } }); });
				},
				[&](const RippleSource& k) {
					json o = json::object({ { "trigger", "@" + k.trigger.name }, { "speed", ParamToJson(k.speed) }, { "width", ParamToJson(k.width) }, { "decay", ParamToJson(k.decay) } });
					if (k.shape != RippleShape::kRing) o["shape"] = NameOf(kRippleShapes, k.shape);
					row["ripple"] = std::move(o);
				});
			return row;
		}

		// ---------------------------------------------------------- outputs

		std::optional<Layer> LayerFrom(const json& a_j, const Ctx& a_ctx)
		{
			if (!a_j.is_object()) {
				a_ctx.Error("a layer must be an object");
				return std::nullopt;
			}
			Layer  l;
			Reader r(a_j, a_ctx);
			if (const auto* src = r.Child("source")) {
				if (src->is_string()) {
					if (auto ref = r.RefFrom(*src, "source")) l.source = *ref;
				} else if (auto value = Reader::ValueFrom(*src, "source", a_ctx); value && Get<Vec3>(*value)) {
					l.source = *Get<Vec3>(*value);
				} else {
					a_ctx.Error("'source' is \"@name\" or [r, g, b]");
				}
			} else {
				a_ctx.Error("'source' is required");
			}
			l.curve = CurveRefFrom(r);
			if (auto b = r.Enum("blend", kBlends)) l.blend = *b;
			if (auto p = r.Parameter("opacity")) l.opacity = *p;
			else if (!r.Has("opacity")) a_ctx.Error("'opacity' is required");
			l.color = r.Vector3("color", true);
			l.mask = r.Reference("mask");
			if (auto channels = r.String("channels")) {
				if (auto set = ChannelSet::Parse(*channels)) {
					l.channels = *set;
				} else {
					a_ctx.Error("'channels' is a subset of \"rgba\"");
				}
			}
			r.Finish();
			return l;
		}

		json LayerToJson(const Layer& a_layer)
		{
			json o = json::object();
			Match(
				a_layer.source,
				[&](const Ref& ref) { o["source"] = "@" + ref.name; },
				[&](const Vec3& c) { o["source"] = PointToJson(c); });
			if (a_layer.curve) o["curve"] = CurveRefToJson(*a_layer.curve);
			if (a_layer.blend != Blend::kReplace) o["blend"] = NameOf(kBlends, a_layer.blend);
			o["opacity"] = ParamToJson(a_layer.opacity);
			if (a_layer.color) o["color"] = VecToJson(*a_layer.color);
			if (a_layer.mask) o["mask"] = "@" + a_layer.mask->name;
			if (a_layer.channels != ChannelSet{}) o["channels"] = a_layer.channels.ToString();
			return o;
		}

		std::optional<Output> OutputFrom(const json& a_j, const Ctx& a_ctx)
		{
			if (!a_j.is_object()) {
				a_ctx.Error("an output must be an object");
				return std::nullopt;
			}
			Reader     r(a_j, a_ctx);
			const auto target = r.Required("target");
			if (target == "light") {
				LightOutput l;
				if (const auto* bones = r.Child("bones")) {
					const auto entry = OneKey(*bones, a_ctx, "'bones'");
					if (entry && entry->key == "skinned") {
						SkinnedBones sb;
						if (entry->value->is_object()) {
							Reader b(*entry->value, a_ctx);
							if (auto m = b.Integer("max")) sb.max = static_cast<std::uint32_t>(std::max(0, *m));
							if (auto share = b.Number("minShare")) sb.minShare = *share;
							b.Finish();
						} else {
							a_ctx.Error("'skinned' takes {\"max\", \"minShare\"}");
						}
						l.bones = sb;
					} else if (entry && entry->key == "named") {
						NamedBones nb;
						if (entry->value->is_array()) {
							for (const auto& b : *entry->value) {
								if (b.is_string()) nb.bones.push_back(b.get<std::string>());
								else a_ctx.Error("'named' entries are bone names");
							}
						} else {
							a_ctx.Error("'named' is an array of bone names");
						}
						l.bones = nb;
					} else if (entry) {
						a_ctx.Error(std::format("'bones' is 'skinned' or 'named', not '{}'", entry->key));
					}
				} else {
					a_ctx.Error("a light needs 'bones'");
				}
				if (auto v = r.Vector3("offset")) l.offset = *v;
				if (auto c = r.Vector3("color", true)) l.color = *c;
				else if (!r.Has("color")) a_ctx.Error("a light needs 'color'");
				if (auto p = r.Parameter("intensity")) l.intensity = *p;
				else if (!r.Has("intensity")) a_ctx.Error("a light needs 'intensity'");
				if (auto p = r.Parameter("size")) l.size = *p;
				if (auto p = r.Parameter("cutoff")) l.cutoff = *p;
				if (auto b = r.Boolean("shadow")) l.shadow = *b;
				if (const auto* bulb = r.Child("bulb")) l.bulb = FormFrom(*bulb, a_ctx, "bulb");
				if (const auto* sel = r.Child("selector")) l.selector = SelectorFrom(*sel, a_ctx);
				if (auto b = r.Boolean("replace")) l.replace = *b;
				r.Finish();
				return Output{ l };
			}
			const auto surface = FromName(kSurfaces, target);
			if (!surface) {
				a_ctx.Error(std::format("'target' is one of {}, light", Choices(kSurfaces)));
				return std::nullopt;
			}
			MaterialOutput m;
			m.surface = *surface;
			if (auto slot = r.Enum("slot", kSlots)) m.slot = *slot;
			else if (!r.Has("slot")) a_ctx.Error("'slot' is required");
			auto& sc = m.scalars;
			sc.strength = r.Parameter("strength");
			sc.scale = r.Parameter("scale");
			sc.color = r.Vector3("color", true);
			sc.weight = r.Parameter("weight");
			sc.screenSpaceScale = r.Parameter("screenSpaceScale");
			sc.logMicrofacetDensity = r.Parameter("logMicrofacetDensity");
			sc.microfacetRoughness = r.Parameter("microfacetRoughness");
			sc.densityRandomization = r.Parameter("densityRandomization");
			sc.roughness = r.Parameter("roughness");
			sc.level = r.Parameter("level");
			sc.thickness = r.Parameter("thickness");
			if (const auto* sel = r.Child("selector")) m.selector = SelectorFrom(*sel, a_ctx);
			if (auto b = r.Boolean("replace")) m.replace = *b;
			if (const auto* stack = r.Child("stack")) {
				if (!stack->is_array()) {
					a_ctx.Error("'stack' must be an array of layers");
				} else {
					std::size_t i = 0;
					for (const auto& layer : *stack) {
						if (auto l = LayerFrom(layer, a_ctx.At(std::format("{} layer {}", a_ctx.where, i)))) {
							m.stack.push_back(std::move(*l));
						}
						++i;
					}
				}
			}
			r.Finish();
			return Output{ m };
		}

		json OutputToJson(const Output& a_output)
		{
			return Match(
				a_output,
				[](const MaterialOutput& m) {
					json o = json::object();
					o["target"] = NameOf(kSurfaces, m.surface);
					o["slot"] = NameOf(kSlots, m.slot);
					const auto& sc = m.scalars;
					if (sc.strength) o["strength"] = ParamToJson(*sc.strength);
					if (sc.scale) o["scale"] = ParamToJson(*sc.scale);
					if (sc.color) o["color"] = VecToJson(*sc.color);
					if (sc.weight) o["weight"] = ParamToJson(*sc.weight);
					if (sc.screenSpaceScale) o["screenSpaceScale"] = ParamToJson(*sc.screenSpaceScale);
					if (sc.logMicrofacetDensity) o["logMicrofacetDensity"] = ParamToJson(*sc.logMicrofacetDensity);
					if (sc.microfacetRoughness) o["microfacetRoughness"] = ParamToJson(*sc.microfacetRoughness);
					if (sc.densityRandomization) o["densityRandomization"] = ParamToJson(*sc.densityRandomization);
					if (sc.roughness) o["roughness"] = ParamToJson(*sc.roughness);
					if (sc.level) o["level"] = ParamToJson(*sc.level);
					if (sc.thickness) o["thickness"] = ParamToJson(*sc.thickness);
					if (!m.selector.All()) o["selector"] = SelectorToJson(m.selector);
					if (m.replace) o["replace"] = true;
					json stack = json::array();
					for (const auto& l : m.stack) {
						stack.push_back(LayerToJson(l));
					}
					o["stack"] = std::move(stack);
					return o;
				},
				[](const LightOutput& l) {
					json o = json::object();
					o["target"] = "light";
					Match(
						l.bones,
						[&](const SkinnedBones& s) {
							json b = json::object({ { "max", s.max } });
							if (s.minShare != 0.0f) b["minShare"] = Num(s.minShare);
							o["bones"] = json::object({ { "skinned", std::move(b) } });
						},
						[&](const NamedBones& n) { o["bones"] = json::object({ { "named", n.bones } }); });
					if (l.offset != Vec3Param{ std::array<Param, 3>{ 0.0f, 0.0f, 0.0f } }) o["offset"] = VecToJson(l.offset);
					o["color"] = VecToJson(l.color);
					o["intensity"] = ParamToJson(l.intensity);
					o["size"] = ParamToJson(l.size);
					o["cutoff"] = ParamToJson(l.cutoff);
					if (l.shadow) o["shadow"] = true;
					if (l.bulb) o["bulb"] = l.bulb->text;
					if (!l.selector.All()) o["selector"] = SelectorToJson(l.selector);
					if (l.replace) o["replace"] = true;
					return o;
				});
		}

		// ------------------------------------------------------------ shell

		ShellSettings ShellFrom(const json& a_j, const Ctx& a_ctx)
		{
			ShellSettings s;
			if (!a_j.is_object()) {
				a_ctx.Error("'shell' must be an object");
				return s;
			}
			Reader r(a_j, a_ctx);
			if (auto m = r.Enum("material", kShellMaterials)) s.material = *m;
			if (auto b = r.Enum("blend", kShellBlends)) s.blend = *b;
			if (auto d = r.Boolean("depthBias")) s.depthBias = *d;
			if (auto t = r.Number("alphaTest")) s.alphaTest = std::clamp(*t, 0.0f, 1.0f);
			if (auto p = r.Parameter("alpha")) s.alpha = *p;
			if (auto p = r.Parameter("rimPower")) s.rimPower = *p;
			if (auto p = r.Parameter("emissive")) s.emissive = *p;
			if (const auto* pose = r.Child("pose")) {
				if (!pose->is_object()) {
					a_ctx.Error("'pose' must be an object");
				} else {
					Reader p(*pose, a_ctx.At("shell pose"));
					if (auto v = p.Vector3("inflate")) s.pose.inflate = *v;
					if (auto v = p.Vector3("offset")) s.pose.offset = *v;
					if (auto sc = p.Parameter("scale")) s.pose.scale = *sc;
					if (auto pt = p.Point("scalePoint")) s.pose.scalePoint = *pt;
					if (auto sp = p.Parameter("spin")) s.pose.spin = *sp;
					if (auto ax = p.Point("spinAxis")) s.pose.spinAxis = *ax;
					p.Finish();
				}
			}
			r.Finish();
			return s;
		}

		json ShellToJson(const ShellSettings& a_shell)
		{
			const ShellSettings defaults;
			json                o = json::object();
			if (a_shell.material != defaults.material) o["material"] = NameOf(kShellMaterials, a_shell.material);
			if (a_shell.blend != defaults.blend) o["blend"] = NameOf(kShellBlends, a_shell.blend);
			if (a_shell.depthBias != defaults.depthBias) o["depthBias"] = a_shell.depthBias;
			if (a_shell.alphaTest != defaults.alphaTest) o["alphaTest"] = Num(a_shell.alphaTest);
			if (a_shell.alpha != defaults.alpha) o["alpha"] = ParamToJson(a_shell.alpha);
			if (a_shell.rimPower != defaults.rimPower) o["rimPower"] = ParamToJson(a_shell.rimPower);
			if (a_shell.emissive != defaults.emissive) o["emissive"] = ParamToJson(a_shell.emissive);
			const auto& p = a_shell.pose;
			const auto& d = defaults.pose;
			json        pose = json::object();
			if (p.inflate != d.inflate) pose["inflate"] = VecToJson(p.inflate);
			if (p.offset != d.offset) pose["offset"] = VecToJson(p.offset);
			if (p.scale != d.scale) pose["scale"] = ParamToJson(p.scale);
			if (p.scalePoint != d.scalePoint) pose["scalePoint"] = PointToJson(p.scalePoint);
			if (p.spin != d.spin) pose["spin"] = ParamToJson(p.spin);
			if (p.spinAxis != d.spinAxis) pose["spinAxis"] = PointToJson(p.spinAxis);
			if (!pose.empty()) o["pose"] = std::move(pose);
			return o;
		}

		// --------------------------------------------------------- variants

		std::optional<Variant> VariantFrom(const json& a_j, const Ctx& a_ctx)
		{
			if (!a_j.is_object()) {
				a_ctx.Error("a variant must be an object");
				return std::nullopt;
			}
			Variant v;
			Reader  r(a_j, a_ctx);
			v.name = r.Required("name");
			const auto ctx = a_ctx.At(std::format("variant {}", v.name));
			if (const auto* key = r.Child("key")) {
				const auto entry = OneKey(*key, ctx, "'key'");
				if (entry && entry->key == "armor") {
					if (auto form = FormFrom(*entry->value, ctx, "armor")) v.key = *form;
				} else if (entry && entry->key == "selector") {
					v.key = SelectorFrom(*entry->value, ctx);
				} else if (entry) {
					ctx.Error(std::format("'key' is 'armor' or 'selector', not '{}'", entry->key));
				}
			} else {
				ctx.Error("a variant needs 'key'");
			}
			if (const auto* overrides = r.Child("overrides")) {
				if (!overrides->is_object()) {
					ctx.Error("'overrides' is an object of signal name to value");
				} else {
					for (const auto& [name, value] : overrides->items()) {
						if (auto val = Reader::ValueFrom(value, name, ctx)) {
							v.overrides[name] = *val;
						}
					}
				}
			}
			r.Finish();
			return v;
		}

		json VariantToJson(const Variant& a_variant)
		{
			json o = json::object({ { "name", a_variant.name } });
			Match(
				a_variant.key,
				[&](const FormRef& armor) { o["key"] = json::object({ { "armor", armor.text } }); },
				[&](const Selector& s) { o["key"] = json::object({ { "selector", SelectorToJson(s) } }); });
			json overrides = json::object();
			for (const auto& [name, value] : a_variant.overrides) {
				overrides[name] = ValueToJson(value);
			}
			o["overrides"] = std::move(overrides);
			return o;
		}

		// ----------------------------------------------------- named rows

		template <class Row, class Parse>
		void NamedRows(Reader& a_root, const char* a_section, const char* a_rowWord, std::vector<Row>& a_out, Parse a_parse)
		{
			const auto* section = a_root.Child(a_section);
			if (!section) {
				return;
			}
			if (!section->is_object()) {
				a_root.Context().Error(std::format("'{}' must be an object keyed by name", a_section));
				return;
			}
			for (const auto& [name, value] : section->items()) {
				if (auto row = a_parse(name, value, a_root.Context().At(std::format("{} {}", a_rowWord, name)))) {
					a_out.push_back(std::move(*row));
				}
			}
		}

		// Duplicate keys inside one object: JSON allows them and most readers
		// keep the last; here they are an error, found while parsing.
		struct DuplicateFinder
		{
			std::vector<std::unordered_set<std::string>> scopes;
			std::vector<std::string>                     duplicates;

			bool operator()(int, json::parse_event_t a_event, json& a_parsed)
			{
				switch (a_event) {
				case json::parse_event_t::object_start:
					scopes.emplace_back();
					break;
				case json::parse_event_t::object_end:
					if (!scopes.empty()) {
						scopes.pop_back();
					}
					break;
				case json::parse_event_t::key:
					if (!scopes.empty() && a_parsed.is_string() && !scopes.back().insert(a_parsed.get<std::string>()).second) {
						duplicates.push_back(a_parsed.get<std::string>());
					}
					break;
				default:
					break;
				}
				return true;
			}
		};
	}

	// ----------------------------------------------------------------- parse

	LoadResult ParseRecipe(std::string_view a_json, std::string_view a_id)
	{
		LoadResult result;
		const Ctx  fileCtx{ result.diagnostics, "file" };

		DuplicateFinder finder;
		json            root = json::parse(a_json, std::ref(finder), false, true);
		if (root.is_discarded() || !root.is_object()) {
			fileCtx.Error("not a JSON object (a syntax error, or the file is not a recipe)");
			return result;
		}
		for (const auto& d : finder.duplicates) {
			fileCtx.Error(std::format("duplicate key '{}'", d));
		}

		Recipe recipe;
		recipe.id = std::string{ a_id };
		const Ctx ctx{ result.diagnostics, "recipe" };
		Reader    r(root, ctx);

		const auto format = r.Integer("format");
		if (!r.Has("format")) {
			ctx.Error(std::format("'format' is required; this loader reads format {}", kRecipeFormat));
		} else if (format && *format > kRecipeFormat) {
			ctx.Error(std::format("format {} is newer than this loader's {}", *format, kRecipeFormat));
			return result;
		}

		auto& meta = recipe.metadata;
		meta.name = r.String("name").value_or("");
		meta.author = r.String("author").value_or("");
		meta.description = r.String("description").value_or("");
		meta.version = r.String("version").value_or("");
		meta.imported = r.String("imported").value_or("");
		if (const auto* m = r.Child("meta")) {
			if (m->is_object()) {
				meta.meta = m->dump();
			} else {
				ctx.Error("'meta' must be an object");
			}
		}

		if (const auto* keys = r.Child("keys")) {
			if (!keys->is_array() || keys->empty()) {
				ctx.Error("'keys' must be a non-empty array");
			} else {
				for (const auto& k : *keys) {
					if (auto key = KeyFrom(k, ctx)) {
						recipe.keys.push_back(std::move(*key));
					}
				}
			}
		} else {
			ctx.Error("'keys' is required");
		}
		recipe.priority = r.Integer("priority");
		if (const auto* clock = r.Child("clock")) {
			Reader c(*clock, ctx.At("clock"));
			if (auto speed = c.Number("speed")) recipe.clock.speed = *speed;
			c.Finish();
		}

		NamedRows(r, "signals", "signal", recipe.signals, [](const std::string& name, const json& j, const Ctx& c) { return SignalFrom(name, j, c); });
		NamedRows(r, "curves", "curve", recipe.curves, [](const std::string& name, const json& j, const Ctx& c) -> std::optional<Curve> {
			if (!j.is_string() || j.get<std::string>().empty()) {
				c.Error("a curve is an expression string in x");
				return std::nullopt;
			}
			return Curve{ name, j.get<std::string>() };
		});
		NamedRows(r, "sources", "source", recipe.sources, [](const std::string& name, const json& j, const Ctx& c) { return SourceFrom(name, j, c); });
		NamedRows(r, "masks", "mask", recipe.masks, [](const std::string& name, const json& j, const Ctx& c) -> std::optional<Mask> {
			if (!j.is_string() || j.get<std::string>().empty()) {
				c.Error("a mask is an expression string over sources");
				return std::nullopt;
			}
			return Mask{ name, j.get<std::string>() };
		});

		if (const auto* outputs = r.Child("outputs")) {
			if (!outputs->is_array()) {
				ctx.Error("'outputs' must be an array");
			} else {
				std::size_t i = 0;
				for (const auto& o : *outputs) {
					if (auto out = OutputFrom(o, ctx.At(std::format("output {}", i)))) {
						recipe.outputs.push_back(std::move(*out));
					}
					++i;
				}
			}
		}
		if (const auto* shell = r.Child("shell")) {
			recipe.shell = ShellFrom(*shell, ctx.At("shell"));
		}
		if (const auto* variants = r.Child("variants")) {
			if (!variants->is_array()) {
				ctx.Error("'variants' must be an array");
			} else {
				std::size_t i = 0;
				for (const auto& v : *variants) {
					if (auto var = VariantFrom(v, ctx.At(std::format("variant {}", i)))) {
						recipe.variants.push_back(std::move(*var));
					}
					++i;
				}
			}
		}
		r.Finish();

		for (auto& d : Validate(recipe)) {
			result.diagnostics.push_back(std::move(d));
		}
		result.recipe = std::move(recipe);
		return result;
	}

	// ------------------------------------------------------------- serialise

	std::string SerializeRecipe(const Recipe& a_recipe)
	{
		json root = json::object();
		root["format"] = kRecipeFormat;
		const auto& meta = a_recipe.metadata;
		if (!meta.name.empty()) root["name"] = meta.name;
		if (!meta.author.empty()) root["author"] = meta.author;
		if (!meta.description.empty()) root["description"] = meta.description;
		if (!meta.version.empty()) root["version"] = meta.version;
		if (!meta.imported.empty()) root["imported"] = meta.imported;
		if (!meta.meta.empty()) {
			json m = json::parse(meta.meta, nullptr, false);
			root["meta"] = m.is_discarded() ? json::object() : m;
		}
		json keys = json::array();
		for (const auto& k : a_recipe.keys) {
			keys.push_back(KeyToJson(k));
		}
		root["keys"] = std::move(keys);
		if (a_recipe.priority) root["priority"] = *a_recipe.priority;
		if (a_recipe.clock != Clock{}) root["clock"] = json::object({ { "speed", Num(a_recipe.clock.speed) } });

		const auto named = [&](const char* a_section, const auto& a_rows, auto a_toJson) {
			if (a_rows.empty()) {
				return;
			}
			json section = json::object();
			for (const auto& row : a_rows) {
				section[row.name] = a_toJson(row);
			}
			root[a_section] = std::move(section);
		};
		named("signals", a_recipe.signals, [](const Signal& s) { return SignalToJson(s); });
		named("curves", a_recipe.curves, [](const Curve& c) { return json(c.text); });
		named("sources", a_recipe.sources, [](const Source& s) { return SourceToJson(s); });
		named("masks", a_recipe.masks, [](const Mask& m) { return json(m.text); });

		if (!a_recipe.outputs.empty()) {
			json outputs = json::array();
			for (const auto& o : a_recipe.outputs) {
				outputs.push_back(OutputToJson(o));
			}
			root["outputs"] = std::move(outputs);
		}
		if (json shell = ShellToJson(a_recipe.shell); !shell.empty()) {
			root["shell"] = std::move(shell);
		}
		if (!a_recipe.variants.empty()) {
			json variants = json::array();
			for (const auto& v : a_recipe.variants) {
				variants.push_back(VariantToJson(v));
			}
			root["variants"] = std::move(variants);
		}
		return root.dump(2) + "\n";
	}
}
