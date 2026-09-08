#include "SettingsCore.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <format>
#include <optional>
#include <sstream>

namespace WornEnchantmentPBR
{
	namespace
	{
		using W = SettingDesc::Widget;

		constexpr std::array kTable{
			SettingDesc{ "General", "PlayerOnly",          "Scope",       "Player only",                 "Apply to the player only, not loaded NPCs.",                                              &Settings::playerOnly,          0, 1, W::kCheckbox,  true  },
			SettingDesc{ "General", "EnableShaders",       "Scope",       "Enabled",                     "Master switch.",                                                                          &Settings::enableShaders,       0, 1, W::kCheckbox,  true  },
			SettingDesc{ "General", "ThirdPerson",         "Scope",       "Third person",                "Drive the normal actor model.",                                                           &Settings::thirdPerson,         0, 1, W::kCheckbox,  true  },
			SettingDesc{ "General", "FirstPerson",         "Scope",       "First person",                "Drive the player's first-person arms.",                                                   &Settings::firstPerson,         0, 1, W::kCheckbox,  true  },
			SettingDesc{ "General", "UniqueMaterial",      "Scope",       "Unique material per clone",   "Give each worn clone its own copy of the pooled PBR material before writing to it.",      &Settings::uniqueMaterial,      0, 1, W::kCheckbox,  true  },
			SettingDesc{ "General", "MaxEffectsPerActor",  "Scope",       "Max effects per actor",       "Safety ceiling for unusual multi-slot armor.",                                            &Settings::maxEffectsPerActor,  1, 256, W::kIntSlider, true },
			SettingDesc{ "General", "VerboseLogging",      "Diagnostics", "Verbose logging",             "One line per geometry on apply, plus restore and ownership diagnostics.",                 &Settings::verboseLogging,      0, 1, W::kCheckbox,  false },
			SettingDesc{ "General", "DebugSolidGlow",      "Diagnostics", "Debug solid glow",            "Ignore the EFSH and glow solid white at Emissive scale. Proves the emissive channel.",   &Settings::debugSolidGlow,      0, 1, W::kCheckbox,  true  },
			SettingDesc{ "General", "AnimationFPS",        "Glow",        "Animation FPS",               "Animation update rate.",                                                                  &Settings::animationFPS,        15, 60, W::kIntSlider, false },
			SettingDesc{ "General", "AnimationSpeed",      "Glow",        "Animation speed",             "Time multiplier for the EFSH animation.",                                                 &Settings::animationSpeed,      Timing::kMinAnimationSpeed, Timing::kMaxAnimationSpeed, W::kSlider, false },
			SettingDesc{ "General", "Intensity",           "Glow",        "Intensity",                   "Alpha multiplier applied before clamping, as in the original plugin.",                    &Settings::intensity,           Timing::kMinIntensity, Timing::kMaxIntensity, W::kSlider, false },
			SettingDesc{ "General", "NormalizeBrightness", "Glow",        "Normalise brightness",        "Every record glows at the same base level: hue from the record, pulse and fade kept, texture mean divided out. Off uses the raw EFSH values times Emissive scale.", &Settings::normalizeBrightness, 0, 1, W::kCheckbox, false },
			SettingDesc{ "General", "EmissiveStrength",    "Glow",        "Emissive strength",           "Normalised mode: emissive multiplier for a texture of mean luminance 0.5.",              &Settings::emissiveStrength,    0, 20, W::kLogSlider, false },
			SettingDesc{ "General", "EmissiveScale",       "Glow",        "Emissive scale",              "Raw mode: multiplier from EFSH fill alpha (vanilla 0.05..0.10) to emissive strength.",   &Settings::emissiveScale,       0, 100, W::kLogSlider, false },
			SettingDesc{ "Colors",  "TintWithEdge",        "Glow",        "Tint with edge colour",       "Vanilla armor enchant shaders keep their colour in the edge effect; the fill keys are grey.", &Settings::tintWithEdge,     0, 1, W::kCheckbox,  false },
			SettingDesc{ "Colors",  "BlackFillAsWhite",    "Glow",        "Black fill as white",         "Stamina and frost shaders have black fill keys; lift them so they show.",                 &Settings::blackFillAsWhite,    0, 1, W::kCheckbox,  false },
			SettingDesc{ "Layers",  "GlowMask",            "Glow",        "Mask by armor channel",       "Multiply the glow per texel by one channel of the armor's RMAOS, rendered per geometry. Needs GPU textures.", &Settings::glowMask, 0, 1, W::kCheckbox, true },
			SettingDesc{ "Layers",  "GlowMaskChannel",     "Glow",        "Mask channel",                "Which RMAOS channel is the mask.",                                                       &Settings::glowMaskChannel,     0, 3, W::kEnum,      false, "roughness (R)\0metallic (G)\0occlusion (B)\0reflectance (A)\0" },
			SettingDesc{ "Layers",  "GlowMaskThreshold",   "Glow",        "Mask threshold",              "0 uses the channel as is; above 0 the mask is on where the channel exceeds this.",       &Settings::glowMaskThreshold,   0, 1, W::kSlider,    false },
			SettingDesc{ "Layers",  "GlowMaskSoftness",    "Glow",        "Mask softness",               "Width of the threshold's soft edge.",                                                    &Settings::glowMaskSoftness,    0.01f, 0.5f, W::kSlider, false },
			SettingDesc{ "Layers",  "GlowMaskInvert",      "Glow",        "Mask inverted",               "Glow where the channel is low instead of high.",                                         &Settings::glowMaskInvert,      0, 1, W::kCheckbox,  false },
			SettingDesc{ "Layers",  "GlowMaskStrength",    "Glow",        "Mask strength",               "0 ignores the mask, 1 applies it fully.",                                                &Settings::glowMaskStrength,    0, 1, W::kSlider,    false },
			SettingDesc{ "Runtime", "RuntimeTextures",     "Runtime",     "Generate layer textures on the GPU", "Exact per-pixel scroll and the sheen map, shimmer and gloss map layers. Off, or on failure, uses the frame folders.", &Settings::runtimeTextures, 0, 1, W::kCheckbox, true },
			SettingDesc{ "Runtime", "RuntimeTextureSize",  "Runtime",     "Texture size",                "Glow and sheen map target size.",                                                         &Settings::runtimeTextureSize,  128, 1024, W::kSizeCombo, true },
			SettingDesc{ "Layers",  "Sheen",               "Sheen",       "Sheen",                       "The EFSH edge effect drives CS's fuzz layer: coloured microflake specular plus an ambient tint.", &Settings::sheen,         0, 1, W::kCheckbox,  true  },
			SettingDesc{ "Layers",  "SheenScale",          "Sheen",       "Sheen scale",                 "Edge alpha (normalised to 1) times this becomes the fuzz weight (0..1).",                &Settings::sheenScale,          0, 4, W::kSlider,    false },
			SettingDesc{ "Layers",  "SheenMap",            "Sheen",       "Sheen map",                   "Scrolling fuzz map: fuzz colour x map.rgb, weight x map.a per texel. Needs GPU textures.", &Settings::sheenMap,          0, 1, W::kCheckbox,  true  },
			SettingDesc{ "Layers",  "SheenMapMirror",      "Sheen",       "Sheen map mirrored",          "Mirror on V so the sheen runs against the glow.",                                         &Settings::sheenMapMirror,      0, 1, W::kCheckbox,  false },
			SettingDesc{ "Layers",  "SheenMapPhase",       "Sheen",       "Sheen map phase",             "Scroll offset relative to the glow, in tiles.",                                           &Settings::sheenMapPhase,       0, 1, W::kSlider,    false },
			SettingDesc{ "Layers",  "Glint",               "Sheen",       "Glint (instead of sheen)",    "Sparkle via CS glints. Mutually exclusive with sheen (sheen wins); changes the shader technique.", &Settings::glint,          0, 1, W::kCheckbox,  true  },
			SettingDesc{ "Layers",  "GlintScreenSpaceScale",     "Sheen", "Glint screen-space scale",    "",                                                                                        &Settings::glintScreenSpaceScale,     0.1f, 20, W::kSlider, true },
			SettingDesc{ "Layers",  "GlintLogMicrofacetDensity", "Sheen", "Glint log microfacet density", "Lower is denser sparkle.",                                                               &Settings::glintLogMicrofacetDensity, 1, 40, W::kSlider,   true },
			SettingDesc{ "Layers",  "GlintMicrofacetRoughness",  "Sheen", "Glint microfacet roughness",  "",                                                                                        &Settings::glintMicrofacetRoughness,  0.001f, 1, W::kLogSlider, true },
			SettingDesc{ "Layers",  "GlintDensityRandomization", "Sheen", "Glint density randomization", "",                                                                                        &Settings::glintDensityRandomization, 0, 10, W::kSlider,   true },
			SettingDesc{ "Layers",  "GlossBoost",          "Gloss",       "Gloss boost",                 "How far roughness drops (0 = off, 1 = mirror at the noise peaks); follows the fill pulse.", &Settings::glossBoost,        0, 1, W::kSlider,    false },
			SettingDesc{ "Layers",  "GlossMap",            "Gloss",       "Gloss map",                   "Re-render the armor's RMAOS with roughness lowered where the scrolling noise is bright. Off = uniform roughness scale.", &Settings::glossMap, 0, 1, W::kCheckbox, true },
			SettingDesc{ "Layers",  "GlossContrast",       "Gloss",       "Gloss contrast",              "Sharpens the noise into patches: 1 = as is, higher = crisper edges.",                     &Settings::glossContrast,       0.1f, 8, W::kSlider,  false },
			SettingDesc{ "Layers",  "GlossMapMirror",      "Gloss",       "Gloss map mirrored",          "",                                                                                        &Settings::glossMapMirror,      0, 1, W::kCheckbox,  false },
			SettingDesc{ "Layers",  "GlossMapTranspose",   "Gloss",       "Gloss map transposed",        "",                                                                                        &Settings::glossMapTranspose,   0, 1, W::kCheckbox,  false },
			SettingDesc{ "Layers",  "GlossMapPhase",       "Gloss",       "Gloss map phase",             "",                                                                                        &Settings::glossMapPhase,       0, 1, W::kSlider,    false },
			SettingDesc{ "Layers",  "GlossMapSize",        "Gloss",       "Gloss map max size",          "The re-rendered RMAOS keeps the armor's resolution up to this.",                          &Settings::glossMapSize,        256, 2048, W::kSizeCombo, true },
			SettingDesc{ "Layers",  "Shimmer",             "Shimmer",     "Shimmer",                     "Scrolling displacement map; enables CS parallax on the material. Needs CS parallax on and GPU textures.", &Settings::shimmer,  0, 1, W::kCheckbox,  true  },
			SettingDesc{ "Layers",  "ShimmerScale",        "Shimmer",     "Shimmer scale",               "Displacement scale; it displaces the whole surface, keep it small.",                      &Settings::shimmerScale,        0, 1, W::kLogSlider, false },
			SettingDesc{ "Layers",  "ShimmerDepthSource",  "Shimmer",     "Shimmer depth source",        "Where the armor relief comes from. Auto = its displacement map, else RMAOS occlusion. Normal slope reads grooves and edges; diffuse reads dark as deep.", &Settings::shimmerDepthSource, 0, 4, W::kEnum, true, "auto\0displacement map\0RMAOS occlusion\0normal slope\0diffuse luminance\0" },
			SettingDesc{ "Layers",  "ShimmerArmorWeight",  "Shimmer",     "Shimmer armor relief",        "Weight of the armor's own depth (its displacement map, else RMAOS occlusion) in the height field.", &Settings::shimmerArmorWeight, 0, 2, W::kSlider, false },
			SettingDesc{ "Layers",  "ShimmerReliefContrast", "Shimmer",   "Shimmer relief contrast",     "Stretches the armor relief around its mean; occlusion maps sit near white and need it.",  &Settings::shimmerReliefContrast, 0.5f, 8, W::kSlider, false },
			SettingDesc{ "Layers",  "ShimmerNoiseWeight",  "Shimmer",     "Shimmer noise",               "Weight of the scrolling noise in the height field.",                                     &Settings::shimmerNoiseWeight,  0, 2, W::kSlider,    false },
			SettingDesc{ "Layers",  "ShimmerTranspose",    "Shimmer",     "Shimmer transposed",          "Swap U and V so the shimmer runs across the glow.",                                       &Settings::shimmerTranspose,    0, 1, W::kCheckbox,  false },
			SettingDesc{ "Layers",  "ShimmerPhase",        "Shimmer",     "Shimmer phase",               "",                                                                                        &Settings::shimmerPhase,        0, 1, W::kSlider,    false },
			SettingDesc{ "Outputs", "Light",               "Light",       "Point light",                 "A point light on the wearer, coloured by the effect hue and driven by its pulse. Third person only.", &Settings::light,   0, 1, W::kCheckbox,  true  },
			SettingDesc{ "Outputs", "LightIntensity",      "Light",       "Light intensity",             "Light fade (brightness) at the pulse's steady state.",                                   &Settings::lightIntensity,      0, 10, W::kSlider,   false },
			SettingDesc{ "Outputs", "LightRadius",         "Light",       "Light radius",                "Light radius in game units.",                                                             &Settings::lightRadius,         50, 2000, W::kIntSlider, false },
			SettingDesc{ "Outputs", "LightBone",           "Light",       "Light bone",                  "Skeleton node the light hangs from.",                                                     &Settings::lightBone,           0, 5, W::kEnum,      true, "worn bones (skin weights)\0spine (chest)\0spine (base)\0pelvis\0head\0root\0" },
			SettingDesc{ "Outputs", "LightMaxPerEffect",   "Light",       "Lights per item",             "With worn bones: at most this many lights, on the bones carrying the most skinned vertices (30% of the top bone or more).", &Settings::lightMaxPerEffect, 1, 4, W::kIntSlider, true },
			SettingDesc{ "Outputs", "LightUseBound",       "Light",       "Light at skinned centre",     "Place each light at the centre of the vertices its bone carries instead of at the bone origin.", &Settings::lightUseBound, 0, 1, W::kCheckbox, true },
			SettingDesc{ "Outputs", "Shell",               "Shell",       "Shell",                       "Clone of each driven geometry with a vanilla rim-lit material, blended over the armor. View-dependent rim without CS changes.", &Settings::shell, 0, 1, W::kCheckbox, true },
			SettingDesc{ "Outputs", "ShellMode",           "Shell",       "Shell mode",                  "Rim: a vanilla rim-lit shell over the layer-driven armor. Layers on shell: the shell gets a private PBR copy that the layers write, and the armor material is never touched.", &Settings::shellMode, 0, 1, W::kEnum, true, "rim shell over driven armor\0layers on the shell\0" },
			SettingDesc{ "Outputs", "ShellDepthBias",      "Shell",       "Shell depth bias",            "Decal flag on the shell so it wins the depth test against the armor. Off relies on inflation for separation.", &Settings::shellDepthBias, 0, 1, W::kCheckbox, true },
			SettingDesc{ "Outputs", "ShellBlend",          "Shell",       "Shell blend",                 "Additive adds the shell's light to the armor; alpha blend covers it.",                   &Settings::shellBlend,          0, 1, W::kEnum,      true, "additive\0alpha blend\0" },
			SettingDesc{ "Outputs", "ShellDiffuse",        "Shell",       "Shell diffuse",               "White shows the shell's lighting, rim and emissive in the effect colour; armor diffuse tints them with the armor.", &Settings::shellDiffuse, 0, 1, W::kEnum, true, "white\0armor diffuse\0" },
			SettingDesc{ "Outputs", "ShellAlpha",          "Shell",       "Shell alpha",                 "Material alpha at the pulse's steady state.",                                            &Settings::shellAlpha,          0, 1, W::kSlider,    false },
			SettingDesc{ "Outputs", "ShellRimPower",       "Shell",       "Shell rim power",             "Vanilla rim lighting exponent; higher is a thinner rim.",                                 &Settings::shellRimPower,       0.5f, 16, W::kLogSlider, false },
			SettingDesc{ "Outputs", "ShellEmissive",       "Shell",       "Shell emissive",              "Emissive multiplier on the shell at the pulse's steady state.",                           &Settings::shellEmissive,       0, 5, W::kSlider,    false },
			SettingDesc{ "Outputs", "ShellScale",          "Shell",       "Shell inflation %",           "Scales the shell about each bone by this percent at the pulse's steady state, per the axis weights below.", &Settings::shellScale, 0, 30, W::kSlider, false },
			SettingDesc{ "Outputs", "ShellScalePulse",     "Shell",       "Shell inflation pulse %",     "Extra inflation per unit of pulse level, so the shell breathes with the effect.",          &Settings::shellScalePulse,     0, 30, W::kSlider,   false },
			SettingDesc{ "Outputs", "ShellScaleAlong",     "Shell",       "Inflation along bone (X)",    "Weight of the inflation along each bone's own axis; 0 keeps the shell's length.",          &Settings::shellScaleAlong,     0, 1, W::kSlider,    false },
			SettingDesc{ "Outputs", "ShellScaleAcrossY",   "Shell",       "Inflation across bone (Y)",   "Weight of the inflation on the bone's Y axis.",                                           &Settings::shellScaleAcrossY,   0, 1, W::kSlider,    false },
			SettingDesc{ "Outputs", "ShellScaleAcrossZ",   "Shell",       "Inflation across bone (Z)",   "Weight of the inflation on the bone's Z axis.",                                           &Settings::shellScaleAcrossZ,   0, 1, W::kSlider,    false },
		};

		std::string_view Trim(std::string_view a_text)
		{
			while (!a_text.empty() && std::isspace(static_cast<unsigned char>(a_text.front()))) {
				a_text.remove_prefix(1);
			}
			while (!a_text.empty() && std::isspace(static_cast<unsigned char>(a_text.back()))) {
				a_text.remove_suffix(1);
			}
			return a_text;
		}

		std::string Lower(std::string_view a_text)
		{
			std::string out{ a_text };
			for (auto& c : out) {
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return out;
		}

		std::optional<bool> ParseBool(std::string_view a_value)
		{
			const auto v = Lower(a_value);
			if (v == "1" || v == "true" || v == "yes" || v == "on") {
				return true;
			}
			if (v == "0" || v == "false" || v == "no" || v == "off") {
				return false;
			}
			return std::nullopt;
		}

		std::optional<float> ParseFloat(std::string_view a_value)
		{
			std::string buf{ a_value };
			char*       end = nullptr;
			const float v = std::strtof(buf.c_str(), &end);
			if (end == buf.c_str() || !Trim({ end }).empty()) {
				return std::nullopt;
			}
			return v;
		}

		std::optional<Timing::Rgb> ParseRgb(std::string_view a_value)
		{
			std::array<float, 3> parts{};
			std::size_t          count = 0;
			std::string          buf{ a_value };
			std::size_t          start = 0;
			while (count < 3) {
				const auto comma = buf.find(',', start);
				const auto piece = Trim(std::string_view{ buf }.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
				const auto f = ParseFloat(piece);
				if (!f) {
					return std::nullopt;
				}
				parts[count++] = *f;
				if (comma == std::string::npos) {
					break;
				}
				start = comma + 1;
			}
			if (count != 3) {
				return std::nullopt;
			}
			if (parts[0] > 1.0f || parts[1] > 1.0f || parts[2] > 1.0f) {
				for (auto& p : parts) {
					p /= 255.0f;
				}
			}
			return Timing::Rgb{ std::clamp(parts[0], 0.0f, 1.0f), std::clamp(parts[1], 0.0f, 1.0f), std::clamp(parts[2], 0.0f, 1.0f) };
		}

		void Assign(Settings& a_settings, const SettingDesc& a_desc, std::string_view a_value)
		{
			std::visit([&](auto a_member) {
				using T = std::remove_cvref_t<decltype(a_settings.*a_member)>;
				if constexpr (std::is_same_v<T, bool>) {
					if (const auto b = ParseBool(a_value)) {
						a_settings.*a_member = *b;
					}
				} else if constexpr (std::is_same_v<T, float>) {
					if (const auto f = ParseFloat(a_value)) {
						a_settings.*a_member = std::clamp(*f, a_desc.min, a_desc.max);
					}
				} else {
					if (const auto f = ParseFloat(a_value)) {
						a_settings.*a_member = static_cast<std::uint32_t>(std::clamp(*f, a_desc.min, a_desc.max));
					}
				}
			},
				a_desc.member);
		}

		std::string Format(const Settings& a_settings, const SettingDesc& a_desc)
		{
			return std::visit([&](auto a_member) -> std::string {
				using T = std::remove_cvref_t<decltype(a_settings.*a_member)>;
				if constexpr (std::is_same_v<T, bool>) {
					return a_settings.*a_member ? "true" : "false";
				} else if constexpr (std::is_same_v<T, float>) {
					return std::format("{:.4g}", a_settings.*a_member);
				} else {
					return std::to_string(a_settings.*a_member);
				}
			},
				a_desc.member);
		}
	}

	std::span<const SettingDesc> SettingTable()
	{
		return kTable;
	}

	Timing::ColorPolicy Settings::ColorPolicyFor(const std::string& a_formKey) const
	{
		Timing::ColorPolicy policy{};
		policy.tintWithEdge = tintWithEdge;
		policy.blackFillAsWhite = blackFillAsWhite;
		if (const auto it = colorOverrides.find(a_formKey); it != colorOverrides.end()) {
			policy.override = it->second;
		}
		return policy;
	}

	Settings Settings::Parse(std::string_view a_text)
	{
		Settings           s{};
		std::istringstream stream{ std::string{ a_text } };
		std::string        line;
		std::string        section = "general";
		while (std::getline(stream, line)) {
			auto text = Trim(line);
			if (text.empty() || text.front() == ';' || text.front() == '#') {
				continue;
			}
			if (text.front() == '[') {
				const auto close = text.find(']');
				section = Lower(Trim(text.substr(1, close == std::string_view::npos ? std::string_view::npos : close - 1)));
				continue;
			}
			const auto eq = text.find('=');
			if (eq == std::string_view::npos) {
				continue;
			}
			const auto key = Lower(Trim(text.substr(0, eq)));
			auto       value = Trim(text.substr(eq + 1));
			if (const auto comment = value.find(';'); comment != std::string_view::npos) {
				value = Trim(value.substr(0, comment));
			}

			bool matched = false;
			for (const auto& desc : kTable) {
				if (section == Lower(desc.section) && key == Lower(desc.key)) {
					Assign(s, desc, value);
					matched = true;
					break;
				}
			}
			if (!matched && section == "colors") {
				if (const auto rgb = ParseRgb(value)) {
					s.colorOverrides[key] = *rgb;
				}
			}
		}
		return s;
	}

	std::string Settings::Serialize() const
	{
		std::string out;
		std::string section;
		for (const auto& desc : kTable) {
			if (section != desc.section) {
				if (!section.empty()) {
					if (section == "Colors") {
						out += "; Per-record overrides: <plugin stem>~<local form id hex> = r,g,b (0..1 or 0..255).\n";
						for (const auto& [key, rgb] : colorOverrides) {
							out += std::format("{}={:.3f},{:.3f},{:.3f}\n", key, rgb.r, rgb.g, rgb.b);
						}
					}
					out += "\n";
				}
				section = desc.section;
				out += std::format("[{}]\n", section);
			}
			if (desc.help && *desc.help) {
				out += std::format("; {}\n", desc.help);
			}
			out += std::format("{}={}\n", desc.key, Format(*this, desc));
		}
		return out;
	}

	bool SettingsDiffer(const Settings& a_lhs, const Settings& a_rhs)
	{
		for (const auto& desc : kTable) {
			if (Format(a_lhs, desc) != Format(a_rhs, desc)) {
				return true;
			}
		}
		if (a_lhs.colorOverrides.size() != a_rhs.colorOverrides.size()) {
			return true;
		}
		for (const auto& [key, rgb] : a_lhs.colorOverrides) {
			const auto it = a_rhs.colorOverrides.find(key);
			if (it == a_rhs.colorOverrides.end() || it->second.r != rgb.r || it->second.g != rgb.g || it->second.b != rgb.b) {
				return true;
			}
		}
		return false;
	}
}
