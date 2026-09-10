#include "recipe/Importer.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects
{
	namespace
	{
		using json = nlohmann::json;

		Vec3 ColorFrom(const json& a_array)
		{
			if (!a_array.is_array() || a_array.size() != 3) {
				return {};
			}
			for (const auto& part : a_array) {
				if (!part.is_number()) {
					return {};
				}
			}
			return Vec3{
				a_array[0].get<float>() / 255.0f,
				a_array[1].get<float>() / 255.0f,
				a_array[2].get<float>() / 255.0f
			};
		}

		float FloatAt(const json& a_object, const char* a_field, float a_default = 0.0f)
		{
			return a_object.contains(a_field) && a_object.at(a_field).is_number() ? a_object.at(a_field).get<float>() : a_default;
		}

		std::string TextAt(const json& a_object, const char* a_field)
		{
			return a_object.contains(a_field) && a_object.at(a_field).is_string() ? a_object.at(a_field).get<std::string>() : std::string{};
		}

		float Chroma(const Vec3& a_color) noexcept
		{
			const float hi = std::max({ a_color.x, a_color.y, a_color.z });
			const float lo = std::min({ a_color.x, a_color.y, a_color.z });
			return hi <= 1e-4f ? 0.0f : (hi - lo) / hi;
		}

		Vec3 NormalizeHue(const Vec3& a_color) noexcept
		{
			const float hi = std::max({ a_color.x, a_color.y, a_color.z });
			if (hi <= 1e-4f) {
				return a_color;
			}
			return Vec3{ a_color.x / hi, a_color.y / hi, a_color.z / hi };
		}

		Vec3 ResolveEmissiveColor(const Vec3& a_fill, const Vec3& a_edge) noexcept
		{
			const float fillPeak = std::max({ a_fill.x, a_fill.y, a_fill.z });
			const float brightness = fillPeak <= 0.02f ? 1.0f : fillPeak;

			if (Chroma(a_edge) <= 0.05f) {
				return fillPeak <= 0.02f ? Vec3{ 1.0f, 1.0f, 1.0f } : a_fill;
			}
			const float edgePeak = std::max({ a_edge.x, a_edge.y, a_edge.z });
			if (edgePeak <= 1e-4f) {
				return a_fill;
			}
			return Vec3{
				a_edge.x / edgePeak * brightness,
				a_edge.y / edgePeak * brightness,
				a_edge.z / edgePeak * brightness
			};
		}

		Vec3 EmissiveHue(const Vec3& a_fill, const Vec3& a_edge) noexcept
		{
			return NormalizeHue(ResolveEmissiveColor(a_fill, a_edge));
		}

		Signal Constant(std::string a_name, float a_value)
		{
			return Signal{ std::move(a_name), ConstantSignal{ a_value }, std::nullopt };
		}

		Signal Constant(std::string a_name, const Vec3& a_value)
		{
			return Signal{ std::move(a_name), ConstantSignal{ a_value }, std::nullopt };
		}

		Signal EfshOf(std::string a_name, EfshField a_field, const FormRef& a_record, std::optional<CurveRef> a_curve = std::nullopt)
		{
			return Signal{ std::move(a_name), EfshSignal{ a_field, a_record }, std::move(a_curve) };
		}

		Signal Expr(std::string a_name, std::string a_text)
		{
			return Signal{ std::move(a_name), ExprSignal{ std::move(a_text) }, std::nullopt };
		}

		struct FieldSpec
		{
			std::string  name;
			ImageChannel channel = ImageChannel::kRgb;
			std::string  scroll;
			bool         mirrorV = false;
			bool         transpose = false;
			float        mip = 0.0f;
		};

		Source Field(const EffectShaderRecord& a_record, FieldSpec a_spec)
		{
			ImageSource image;
			image.path = a_record.fillTexture;
			image.channel = a_spec.channel;
			image.scroll = Vec2Param{ Ref{ std::move(a_spec.scroll) } };
			image.tile = Vec2Param{ std::array<Param, 2>{ std::max(a_record.tileU, 0.01f), std::max(a_record.tileV, 0.01f) } };
			image.mirror = { false, a_spec.mirrorV };
			image.transpose = a_spec.transpose;
			image.mip = a_spec.mip;
			return Source{ std::move(a_spec.name), image };
		}

		Layer StackLayer(LayerSource a_source, Blend a_blend, Param a_opacity)
		{
			Layer l;
			l.source = std::move(a_source);
			l.blend = a_blend;
			l.opacity = std::move(a_opacity);
			return l;
		}

		Ref At(const char* a_name)
		{
			return Ref{ a_name };
		}

		std::string Num(float a_value)
		{
			return std::format("{}", a_value);
		}

		inline constexpr float             kEmissiveStrength = 1.0f;
		inline constexpr float             kSheenScale = 0.5f;
		inline constexpr float             kSheenPhase = 0.5f;
		inline constexpr bool              kSheenMirrorV = true;
		inline constexpr float             kShimmerScale = 0.1f;
		inline constexpr float             kShimmerPhase = 0.25f;
		inline constexpr bool              kShimmerTranspose = true;
		inline constexpr float             kShimmerNoiseWeight = 0.35f;
		inline constexpr float             kShimmerReliefContrast = 3.0f;
		inline constexpr float             kGlossBoost = 0.25f;
		inline constexpr float             kGlossContrast = 2.0f;
		inline constexpr float             kLightSize = 1.4f;
		inline constexpr float             kLightCutoff = 0.05f;
		inline constexpr std::uint32_t     kLightMaxPerEffect = 2;
		inline constexpr float             kShellAlpha = 0.5f;
		inline constexpr float             kShellInflatePercent = 1.0f;
		inline constexpr float             kShellInflatePulsePercent = 1.0f;
		inline constexpr MaterialChannel   kGlowMaskChannel = MaterialChannel::kMetallic;
		inline constexpr std::string_view  kImporter = "BetterEnchantmentEffects 0.1.0";

		std::vector<Curve> DefaultCurves(const Efsh::EffectParams& a_p)
		{
			std::vector<Curve> curves;
			curves.push_back({ "rest", std::format("x / {}", Num(Efsh::BaselineAlpha(a_p.fill))) });
			curves.push_back({ "edgeRest", std::format("x / {}", Num(Efsh::BaselineAlpha(a_p.edge))) });
			curves.push_back({ "crisp", std::format("(x - mean) * {} + 0.5", Num(kShimmerReliefContrast)) });
			curves.push_back({ "punchy", std::format("(x - mean) * {} + 0.5", Num(kGlossContrast)) });
			return curves;
		}

		std::vector<Signal> DefaultSignals(const FormRef& a_record, const Vec3& a_hue, bool a_hasFill)
		{
			std::vector<Signal> signals;
			signals.push_back(EfshOf("fillLevel", EfshField::kFillAlpha, a_record, CurveRef{ "@rest" }));
			signals.push_back(EfshOf("edgeLevel", EfshField::kEdgeAlpha, a_record, CurveRef{ "@edgeRest" }));
			signals.push_back(EfshOf("edgeColor", EfshField::kEdgeColor, a_record));
			signals.push_back(EfshOf("scroll", EfshField::kScroll, a_record));

			signals.push_back(Constant("glowHue", a_hue));
			signals.push_back(Constant("glowStrength", kEmissiveStrength));
			signals.push_back(Expr("glowLevel", "@glowStrength * @fillLevel"));

			signals.push_back(Constant("sheenScale", kSheenScale));
			signals.push_back(Expr("sheenWeight", "clamp(@edgeLevel * @sheenScale, 0, 1)"));
			if (a_hasFill) signals.push_back(Expr("sheenScroll", std::format("@scroll + {}", Num(kSheenPhase))));

			signals.push_back(Constant("shimmerScale", kShimmerScale));
			signals.push_back(Expr("heightScale", "@shimmerScale * clamp(@fillLevel, 0, 2)"));
			if (a_hasFill) signals.push_back(Expr("shimmerScroll", std::format("@scroll + {}", Num(kShimmerPhase))));

			if (a_hasFill) {
				signals.push_back(Constant("glossBoost", kGlossBoost));
				signals.push_back(Expr("glossAmount", "@glossBoost * saturate(@fillLevel)"));
			}

			signals.push_back(Expr("lightLevel", "clamp(@fillLevel, 0, 2)"));
			signals.push_back(Expr("shellOpacity", std::format("saturate({} * clamp(@fillLevel, 0, 2))", Num(kShellAlpha))));
			signals.push_back(Expr("inflate", std::format("({} + {} * clamp(@fillLevel, 0, 2)) * 0.01", Num(kShellInflatePercent), Num(kShellInflatePulsePercent))));
			return signals;
		}

		std::vector<Source> DefaultSources(const EffectShaderRecord& a_record, bool a_hasFill)
		{
			std::vector<Source> sources;
			if (a_hasFill) {
				sources.push_back(Field(a_record, { .name = "fill", .channel = ImageChannel::kRgb, .scroll = "scroll" }));
				sources.push_back(Field(a_record, { .name = "sheenField", .channel = ImageChannel::kLuma, .scroll = "sheenScroll", .mirrorV = kSheenMirrorV }));
				sources.push_back(Field(a_record, { .name = "shimmerField", .channel = ImageChannel::kLuma, .scroll = "shimmerScroll", .transpose = kShimmerTranspose, .mip = 2.0f }));
				sources.push_back(Field(a_record, { .name = "glossField", .channel = ImageChannel::kLuma, .scroll = "scroll", .mip = 1.0f }));
			}
			sources.push_back(Source{ "relief", MaterialSource{ MaterialChannel::kRelief } });
			sources.push_back(Source{ "metallic", MaterialSource{ kGlowMaskChannel } });
			return sources;
		}

		SurfaceOutput GlowOutput(bool a_hasFill)
		{
			SurfaceOutput o;
			o.surface = Surface::kShell;
			o.slot = Slot::kEmissive;
			o.scalars.strength = At("glowLevel");
			Layer l = StackLayer(a_hasFill ? LayerSource{ At("fill") } : LayerSource{ Vec3{ 1.0f, 1.0f, 1.0f } }, Blend::kReplace, 1.0f);
			l.color = Vec3Param{ At("glowHue") };
			l.mask = At("metal");
			o.stack.push_back(std::move(l));
			return o;
		}

		SurfaceOutput SheenOutput(bool a_hasFill)
		{
			SurfaceOutput o;
			o.surface = Surface::kShell;
			o.slot = Slot::kFuzz;
			o.scalars.color = Vec3Param{ At("edgeColor") };
			o.scalars.weight = At("sheenWeight");
			o.stack.push_back(StackLayer(Vec3{ 1.0f, 1.0f, 1.0f }, Blend::kReplace, 1.0f));
			if (a_hasFill) {
				Layer alpha = StackLayer(At("sheenField"), Blend::kReplace, 1.0f);
				alpha.channels = ChannelSet{ false, false, false, true };
				o.stack.push_back(std::move(alpha));
			}
			return o;
		}

		SurfaceOutput HeightOutput(bool a_hasFill)
		{
			SurfaceOutput o;
			o.surface = Surface::kMaterial;
			o.slot = Slot::kHeight;
			o.scalars.scale = At("heightScale");
			Layer relief = StackLayer(At("relief"), Blend::kReplace, 1.0f);
			relief.curve = CurveRef{ "@crisp" };
			o.stack.push_back(std::move(relief));
			if (a_hasFill) {
				o.stack.push_back(StackLayer(At("shimmerField"), Blend::kAdd, kShimmerNoiseWeight));
			}
			return o;
		}

		SurfaceOutput GlossOutput()
		{
			SurfaceOutput o;
			o.surface = Surface::kMaterial;
			o.slot = Slot::kRmaos;
			Layer gloss = StackLayer(At("glossField"), Blend::kSubtract, At("glossAmount"));
			gloss.curve = CurveRef{ "@punchy" };
			gloss.channels = ChannelSet{ true, false, false, false };
			o.stack.push_back(std::move(gloss));
			return o;
		}

		LightOutput GlowLight()
		{
			LightOutput light;
			light.bones = SkinnedBones{ kLightMaxPerEffect, 0.0f };
			light.color = Vec3Param{ At("glowHue") };
			light.intensity = At("lightLevel");
			light.size = kLightSize;
			light.cutoff = kLightCutoff;
			return light;
		}

		ShellSettings DefaultShell()
		{
			ShellSettings shell;
			shell.material = ShellMaterial::kPbrCopy;
			shell.blend = ShellBlend::kAdditive;
			shell.depthBias = true;
			shell.alpha = At("shellOpacity");
			shell.pose.inflate = std::array<Param, 3>{ 0.0f, At("inflate"), At("inflate") };
			return shell;
		}
	}

	FormRef EffectShaderRecord::Reference() const
	{
		return FormRef::From(editorId.empty() ? key.ToString() : editorId);
	}

	std::expected<EffectShaderRecord, std::string> ParseEffectShaderRecord(std::string_view a_json)
	{
		const json root = json::parse(a_json, nullptr, false);
		if (root.is_discarded() || !root.is_object()) {
			return std::unexpected("not a JSON object");
		}
		EffectShaderRecord r;
		const std::optional<FormKey> key = FormKey::Parse(TextAt(root, "formKey"));
		if (!key) {
			return std::unexpected("'formKey' is missing or malformed (expected 0x<id>~<plugin>)");
		}
		r.key = *key;
		r.editorId = TextAt(root, "editorId");
		r.fillTexture = TextAt(root, "fillTexture");

		Efsh::EffectParams& p = r.params;
		p.colorKeys = { ColorFrom(root.value("fillColorKey1", json::array())), ColorFrom(root.value("fillColorKey2", json::array())), ColorFrom(root.value("fillColorKey3", json::array())) };
		p.colorKeyTimes = { FloatAt(root, "fillColorKey1Time"), FloatAt(root, "fillColorKey2Time"), FloatAt(root, "fillColorKey3Time") };
		p.colorKeyScales = { FloatAt(root, "fillColorKey1Scale", 1.0f), FloatAt(root, "fillColorKey2Scale", 1.0f), FloatAt(root, "fillColorKey3Scale", 1.0f) };
		p.colorScale = FloatAt(root, "colorScale", 1.0f);
		p.fill.fullAlphaRatio = FloatAt(root, "fillFullAlphaRatio", 1.0f);
		p.fill.persistentAlphaRatio = FloatAt(root, "fillPersistentAlphaRatio", 1.0f);
		p.fill.pulseAmplitude = FloatAt(root, "fillAlphaPulseAmplitude");
		p.fill.pulseFrequency = FloatAt(root, "fillAlphaPulseFrequency");
		p.fill.fadeInTime = FloatAt(root, "fillAlphaFadeInTime");
		p.animationSpeedU = FloatAt(root, "fillTextureAnimationSpeedU");
		p.animationSpeedV = FloatAt(root, "fillTextureAnimationSpeedV");
		p.edgeColor = ColorFrom(root.value("edgeColor", json::array()));
		p.edge.fullAlphaRatio = FloatAt(root, "edgeFullAlphaRatio", 1.0f);
		p.edge.persistentAlphaRatio = FloatAt(root, "edgePersistentAlphaRatio", 1.0f);
		p.edge.pulseAmplitude = FloatAt(root, "edgeAlphaPulseAmplitude");
		p.edge.pulseFrequency = FloatAt(root, "edgeAlphaPulseFrequency");
		p.edge.fadeInTime = FloatAt(root, "edgeAlphaFadeInTime");
		r.tileU = FloatAt(root, "fillTextureScaleU", 1.0f);
		r.tileV = FloatAt(root, "fillTextureScaleV", 1.0f);
		return r;
	}

	std::string RecipeIdFor(const EffectShaderRecord& a_record)
	{
		if (!a_record.editorId.empty()) {
			return a_record.editorId;
		}
		std::string stem = a_record.key.file;
		if (const auto dot = stem.find_last_of('.'); dot != std::string::npos) {
			stem.erase(dot);
		}
		return std::format("{}-{:X}", stem, a_record.key.localId);
	}

	Recipe ImportEffectShader(const EffectShaderRecord& a_record)
	{
		const FormRef record = a_record.Reference();
		const bool    hasFill = !a_record.fillTexture.empty();
		const Vec3    hue = EmissiveHue(a_record.params.colorKeys[0], a_record.params.edgeColor);

		Recipe r;
		r.id = RecipeIdFor(a_record);
		r.metadata.name = a_record.editorId.empty() ? a_record.key.ToString() : a_record.editorId;
		r.metadata.description = std::format("Imported from effect shader {} with the shipped defaults.", record.text);
		r.metadata.imported = std::string{ kImporter };
		r.keys.push_back(RecipeKey{ KeyKind::kEffectShader, record });

		r.curves = DefaultCurves(a_record.params);
		r.signals = DefaultSignals(record, hue, hasFill);
		r.sources = DefaultSources(a_record, hasFill);
		r.masks.push_back(Mask{ "metal", "@metallic" });

		r.outputs.emplace_back(GlowOutput(hasFill));
		r.outputs.emplace_back(SheenOutput(hasFill));
		r.outputs.emplace_back(HeightOutput(hasFill));
		if (hasFill) {
			r.outputs.emplace_back(GlossOutput());
		}
		r.outputs.emplace_back(GlowLight());

		r.shell = DefaultShell();
		return r;
	}
}
