#include "Importer.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace WornEnchantmentPBR
{
	namespace
	{
		Signal Constant(std::string a_name, float a_value)
		{
			return Signal{ std::move(a_name), ConstantSignal{ a_value }, std::nullopt };
		}

		Signal Constant(std::string a_name, const Vec3& a_value)
		{
			return Signal{ std::move(a_name), ConstantSignal{ a_value }, std::nullopt };
		}

		Signal Efsh(std::string a_name, EfshField a_field, const FormRef& a_record, std::optional<CurveRef> a_curve = std::nullopt)
		{
			return Signal{ std::move(a_name), EfshSignal{ a_field, a_record }, std::move(a_curve) };
		}

		Signal Expr(std::string a_name, std::string a_text)
		{
			return Signal{ std::move(a_name), ExprSignal{ std::move(a_text) }, std::nullopt };
		}

		Source Field(std::string a_name, const EffectShaderRecord& a_record, ImageChannel a_channel, std::string a_scroll, bool a_mirrorV, bool a_transpose, float a_mip)
		{
			ImageSource image;
			image.path = a_record.fillTexture;
			image.channel = a_channel;
			image.scroll = Vec2Param{ Ref{ std::move(a_scroll) } };
			image.tile = Vec2Param{ std::array<Param, 2>{ std::max(a_record.tileU, 0.01f), std::max(a_record.tileV, 0.01f) } };
			image.mirror = { false, a_mirrorV };
			image.transpose = a_transpose;
			image.mip = a_mip;
			return Source{ std::move(a_name), image };
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
			std::string text = std::format("{}", a_value);
			return text;
		}
	}

	FormRef EffectShaderRecord::Reference() const
	{
		return FormRef::From(editorId.empty() ? key.ToString() : editorId);
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

	Recipe ImportEffectShader(const EffectShaderRecord& a_record, const ImportDefaults& a_defaults)
	{
		const auto  record = a_record.Reference();
		const auto& p = a_record.params;
		const bool  hasFill = !a_record.fillTexture.empty();
		Recipe      r;
		r.id = RecipeIdFor(a_record);
		r.metadata.name = a_record.editorId.empty() ? a_record.key.ToString() : a_record.editorId;
		r.metadata.description = std::format("Imported from effect shader {} with the shipped defaults.", record.text);
		r.metadata.imported = a_defaults.importer;
		r.keys.push_back(RecipeKey{ KeyKind::kEffectShader, record });

		r.curves.push_back({ "rest", std::format("x / {}", Num(Timing::BaselineAlpha(p.fill))) });
		r.curves.push_back({ "edgeRest", std::format("x / {}", Num(Timing::BaselineAlpha(p.edge))) });
		r.curves.push_back({ "crisp", std::format("(x - mean) * {} + 0.5", Num(a_defaults.shimmerReliefContrast)) });
		r.curves.push_back({ "punchy", std::format("(x - mean) * {} + 0.5", Num(a_defaults.glossContrast)) });

		r.signals.push_back(Efsh("fillLevel", EfshField::kFillAlpha, record, CurveRef{ "@rest" }));
		r.signals.push_back(Efsh("edgeLevel", EfshField::kEdgeAlpha, record, CurveRef{ "@edgeRest" }));
		r.signals.push_back(Efsh("edgeColor", EfshField::kEdgeColor, record));
		r.signals.push_back(Efsh("scroll", EfshField::kScroll, record));

		const auto hue = Timing::NormalizeHue(Timing::ResolveEmissiveColor(p.colorKeys[0], p.edgeColor, a_defaults.colorPolicy));
		r.signals.push_back(Constant("glowHue", Vec3{ hue.r, hue.g, hue.b }));
		r.signals.push_back(Constant("glowStrength", a_defaults.emissiveStrength));
		r.signals.push_back(Expr("glowLevel", "@glowStrength * @fillLevel"));

		r.signals.push_back(Constant("sheenScale", a_defaults.sheenScale));
		r.signals.push_back(Expr("sheenWeight", "clamp(@edgeLevel * @sheenScale, 0, 1)"));
		if (hasFill) r.signals.push_back(Expr("sheenScroll", std::format("@scroll + {}", Num(a_defaults.sheenPhase))));

		r.signals.push_back(Constant("shimmerScale", a_defaults.shimmerScale));
		r.signals.push_back(Expr("heightScale", "@shimmerScale * clamp(@fillLevel, 0, 2)"));
		if (hasFill) r.signals.push_back(Expr("shimmerScroll", std::format("@scroll + {}", Num(a_defaults.shimmerPhase))));

		if (hasFill) {
			r.signals.push_back(Constant("glossBoost", a_defaults.glossBoost));
			r.signals.push_back(Expr("glossAmount", "@glossBoost * saturate(@fillLevel)"));
		}

		r.signals.push_back(Expr("lightLevel", "clamp(@fillLevel, 0, 2)"));
		r.signals.push_back(Expr("shellOpacity", std::format("saturate({} * clamp(@fillLevel, 0, 2))", Num(a_defaults.shellAlpha))));
		r.signals.push_back(Expr("inflate", std::format("({} + {} * clamp(@fillLevel, 0, 2)) * 0.01", Num(a_defaults.shellInflatePercent), Num(a_defaults.shellInflatePulsePercent))));

		if (hasFill) {
			r.sources.push_back(Field("fill", a_record, ImageChannel::kRgb, "scroll", false, false, 0.0f));
			r.sources.push_back(Field("sheenField", a_record, ImageChannel::kLuma, "sheenScroll", a_defaults.sheenMirrorV, false, 0.0f));
			r.sources.push_back(Field("shimmerField", a_record, ImageChannel::kLuma, "shimmerScroll", false, a_defaults.shimmerTranspose, 2.0f));
			r.sources.push_back(Field("glossField", a_record, ImageChannel::kLuma, "scroll", false, false, 1.0f));
		}
		r.sources.push_back(Source{ "relief", MaterialSource{ MaterialChannel::kRelief } });
		r.sources.push_back(Source{ "metallic", MaterialSource{ a_defaults.glowMaskChannel } });

		r.masks.push_back(Mask{ "metal", "@metallic" });

		{
			SurfaceOutput o;
			o.surface = Surface::kShell;
			o.slot = Slot::kEmissive;
			o.scalars.strength = At("glowLevel");
			auto l = StackLayer(hasFill ? LayerSource{ At("fill") } : LayerSource{ Vec3{ 1.0f, 1.0f, 1.0f } }, Blend::kReplace, 1.0f);
			l.color = Vec3Param{ At("glowHue") };
			l.mask = At("metal");
			o.stack.push_back(std::move(l));
			r.outputs.push_back(std::move(o));
		}
		{
			SurfaceOutput o;
			o.surface = Surface::kShell;
			o.slot = Slot::kFuzz;
			o.scalars.color = Vec3Param{ At("edgeColor") };
			o.scalars.weight = At("sheenWeight");
			o.stack.push_back(StackLayer(Vec3{ 1.0f, 1.0f, 1.0f }, Blend::kReplace, 1.0f));
			if (hasFill) {
				auto alpha = StackLayer(At("sheenField"), Blend::kReplace, 1.0f);
				alpha.channels = ChannelSet{ false, false, false, true };
				o.stack.push_back(std::move(alpha));
			}
			r.outputs.push_back(std::move(o));
		}
		{
			SurfaceOutput o;
			o.surface = Surface::kMaterial;
			o.slot = Slot::kHeight;
			o.scalars.scale = At("heightScale");
			auto relief = StackLayer(At("relief"), Blend::kReplace, 1.0f);
			relief.curve = CurveRef{ "@crisp" };
			o.stack.push_back(std::move(relief));
			if (hasFill) {
				o.stack.push_back(StackLayer(At("shimmerField"), Blend::kAdd, a_defaults.shimmerNoiseWeight));
			}
			r.outputs.push_back(std::move(o));
		}
		if (hasFill) {
			SurfaceOutput o;
			o.surface = Surface::kMaterial;
			o.slot = Slot::kRmaos;
			auto gloss = StackLayer(At("glossField"), Blend::kSubtract, At("glossAmount"));
			gloss.curve = CurveRef{ "@punchy" };
			gloss.channels = ChannelSet{ true, false, false, false };
			o.stack.push_back(std::move(gloss));
			r.outputs.push_back(std::move(o));
		}
		{
			LightOutput light;
			light.bones = SkinnedBones{ a_defaults.lightMaxPerEffect, 0.0f };
			light.color = Vec3Param{ At("glowHue") };
			light.intensity = At("lightLevel");
			light.size = a_defaults.lightSize;
			light.cutoff = a_defaults.lightCutoff;
			r.outputs.push_back(light);
		}

		r.shell.material = ShellMaterial::kPbrCopy;
		r.shell.blend = ShellBlend::kAdditive;
		r.shell.depthBias = true;
		r.shell.alpha = At("shellOpacity");
		r.shell.pose.inflate = std::array<Param, 3>{ 0.0f, At("inflate"), At("inflate") };
		return r;
	}
}
