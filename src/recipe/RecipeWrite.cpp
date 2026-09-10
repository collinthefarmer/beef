#include "recipe/Recipe.h"
#include "recipe/Words.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <cstdlib>
#include <format>
#include <string>
#include <utility>

namespace BetterEnchantmentEffects
{
	using json = nlohmann::ordered_json;

	namespace
	{
		float ClusterWeight(const MaterialClustersSource& a_source, std::string_view a_field) noexcept
		{
			if (a_field == "roughness") return a_source.roughness;
			if (a_field == "metallic") return a_source.metallic;
			if (a_field == "occlusion") return a_source.occlusion;
			if (a_field == "reflectance") return a_source.reflectance;
			if (a_field == "luma") return a_source.luma;
			return 0.0f;
		}
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
					[](const BoneWeightBake& b) { return std::format("bake boneWeight of {} bone(s)", b.bones.size()); },
					[](const ComponentIdBake&) { return std::string{ "bake componentId (the mesh's connected pieces, id / 255)" }; },
					[](const ChartIdBake&) { return std::string{ "bake chartId (the mesh's UV charts, id / 255)" }; });
			},
			[](const UvSource& s) { return std::format("uv {} (the coordinate as a ramp over the islands)", s.axis == UvAxis::kU ? "u" : "v"); },
			[](const DistanceSource& s) {
				return Match(
					s.from,
					[](const std::string& node) { return std::format("distance from node {} (bind pose, 0..256 units as 0..1)", node); },
					[](const Vec3& p) { return std::format("distance from ({:.0f}, {:.0f}, {:.0f}) (bind pose, 0..256 units as 0..1)", p.x, p.y, p.z); });
			},
			[](const RippleSource& s) { return std::format("ripple {} from @{}, speed {}, width {}, decay {}", s.shape == RippleShape::kDisc ? "disc" : "ring", s.trigger.name, ParamText(s.speed), ParamText(s.width), ParamText(s.decay)); },
			[](const MaterialClustersSource& s) {
				const MaterialClustersSource defaults;
				std::string                  text = std::format("materialClusters, {} clusters", s.clusters);
				for (const auto& [weight, field] : { std::pair{ s.roughness, "roughness" }, std::pair{ s.metallic, "metallic" }, std::pair{ s.occlusion, "occlusion" }, std::pair{ s.reflectance, "reflectance" }, std::pair{ s.luma, "luma" } }) {
					if (weight != ClusterWeight(defaults, field)) {
						text += std::format(", {} {}", field, weight);
					}
				}
				if (s.seed != defaults.seed) {
					text += std::format(", seed {}", s.seed);
				}
				if (s.iterations != defaults.iterations) {
					text += std::format(", {} iterations", s.iterations);
				}
				return text;
			});
	}

	namespace
	{
		json Num(float a_value)
		{
			char       buffer[32];
			const auto r = std::to_chars(buffer, buffer + sizeof(buffer), a_value);
			return json(std::strtod(std::string(buffer, r.ptr).c_str(), nullptr));
		}

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

		json KeyToJson(const RecipeKey& a_key)
		{
			const std::string word{ NameOf(kKeyKinds, a_key.kind) };
			return Match(
				a_key.operand,
				[&](const std::monostate&) { return json(word); },
				[&](const FormRef& form) { return json::object({ { word, form.text } }); },
				[&](const std::string& glob) { return json::object({ { word, glob } }); });
		}

		json SelectorToJson(const Selector& a_selector)
		{
			json out = json::array();
			for (const auto& t : a_selector.anyOf) {
				const std::string value = Match(t.operand, [](const FormRef& form) { return form.text; }, [](const std::string& glob) { return glob; });
				out.push_back(json::object({ { std::string{ NameOf(kSelectorKinds, t.kind) }, value } }));
			}
			return out;
		}

		struct Writer
		{
			json& out;

			void Write(std::string_view a_key, std::string_view a_value) { out[std::string{ a_key }] = std::string{ a_value }; }
			void Write(std::string_view a_key, const Param& a_value) { out[std::string{ a_key }] = ParamToJson(a_value); }
			void Write(std::string_view a_key, const Param& a_value, const Param& a_default) { if (a_value != a_default) Write(a_key, a_value); }
			void Write(std::string_view a_key, const Vec3Param& a_value, const Vec3Param& a_default) { if (a_value != a_default) out[std::string{ a_key }] = VecToJson(a_value); }
			void Write(std::string_view a_key, const std::optional<Vec2Param>& a_value) { if (a_value) out[std::string{ a_key }] = VecToJson(*a_value); }
			void Write(std::string_view a_key, bool a_value, bool a_default) { if (a_value != a_default) out[std::string{ a_key }] = a_value; }
			void Write(std::string_view a_key, std::uint32_t a_value, std::uint32_t a_default) { if (a_value != a_default) out[std::string{ a_key }] = a_value; }
			void WriteNumber(std::string_view a_key, float a_value, float a_default) { if (a_value != a_default) out[std::string{ a_key }] = Num(a_value); }
			void WritePoint(std::string_view a_key, const Vec3& a_value, const Vec3& a_default) { if (!(a_value == a_default)) out[std::string{ a_key }] = PointToJson(a_value); }

			template <class Row, std::size_t N, class E>
			void Write(std::string_view a_key, const Row (&a_table)[N], E a_value, E a_default) { if (a_value != a_default) out[std::string{ a_key }] = NameOf(a_table, a_value); }
		};

		json PulseToJson(const PulseSignal& k)
		{
			json o = json::object();
			o["base"] = ParamToJson(k.base);
			o["amplitude"] = ParamToJson(k.amplitude);
			o["period"] = ParamToJson(k.period);
			if (k.phase != Param{ 0.0f }) o["phase"] = ParamToJson(k.phase);
			if (k.waveform != Waveform::kSine) o["waveform"] = NameOf(kWaveforms, k.waveform);
			return o;
		}

		json ActorValueToJson(const ActorValueSignal& k)
		{
			if (k.measure == Measure::kCurrent) {
				return json(k.actorValue);
			}
			return json::object({ { "of", k.actorValue }, { "measure", NameOf(kMeasures, k.measure) } });
		}

		json TriggerToJson(const TriggerSignal& k)
		{
			json o = json::object();
			Match(
				k.origin,
				[&](const EventOrigin& e) {
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
				[&](const PluginOrigin& p) { o["plugin"] = p.id; },
				[&](const WhenOrigin& w) {
					o["when"] = "@" + w.when.name;
					if (w.value) o["value"] = "@" + w.value->name;
				});
			o["lifetime"] = ParamToJson(k.lifetime);
			o["max"] = k.max;
			return o;
		}

		json CounterToJson(const CounterSignal& k)
		{
			json o = json::object({ { "trigger", "@" + k.trigger.name } });
			if (k.reset) o["reset"] = "@" + k.reset->name;
			if (k.cap) o["cap"] = ParamToJson(*k.cap);
			return o;
		}

		json NoiseToJson(const NoiseSignal& k)
		{
			json o = json::object({ { "frequency", ParamToJson(k.frequency) }, { "amplitude", ParamToJson(k.amplitude) } });
			if (k.seed != 0) o["seed"] = k.seed;
			return o;
		}

		json GradientToJson(const GradientSignal& k)
		{
			json stops = json::array();
			for (const auto& s : k.stops) {
				stops.push_back(json::object({ { "at", Num(s.at) }, { "color", VecToJson(s.color) } }));
			}
			return json::object({ { "t", ParamToJson(k.t) }, { "stops", std::move(stops) } });
		}

		json SignalToJson(const Signal& a_signal)
		{
			json       row = json::object();
			const auto key = [](SignalKindId a_id) { return std::string{ SignalKindName(a_id) }; };
			Match(
				a_signal.kind,
				[&](const ConstantSignal& k) { row[key(SignalKindId::kConstant)] = ValueToJson(k.value); },
				[&](const PulseSignal& k) { row[key(SignalKindId::kPulse)] = PulseToJson(k); },
				[&](const RampSignal& k) { row[key(SignalKindId::kRamp)] = json::object({ { "from", ParamToJson(k.from) }, { "to", ParamToJson(k.to) }, { "seconds", ParamToJson(k.seconds) } }); },
				[&](const EfshSignal& k) { row[key(SignalKindId::kEfsh)] = json::object({ { "field", NameOf(kEfshFields, k.field) }, { "record", k.record.text } }); },
				[&](const ActorValueSignal& k) { row[key(SignalKindId::kActorValue)] = ActorValueToJson(k); },
				[&](const ActorStateSignal& k) { row[key(SignalKindId::kActorState)] = NameOf(kActorStates, k.kind); },
				[&](const EnchantmentSignal& k) { row[key(SignalKindId::kEnchantment)] = NameOf(kEnchantmentFields, k.field); },
				[&](const TriggerSignal& k) { row[key(SignalKindId::kTrigger)] = TriggerToJson(k); },
				[&](const PayloadSignal& k) { row[key(SignalKindId::kPayload)] = json::object({ { "trigger", "@" + k.trigger.name }, { "field", NameOf(kPayloadFields, k.field) } }); },
				[&](const CounterSignal& k) { row[key(SignalKindId::kCounter)] = CounterToJson(k); },
				[&](const AccumulateSignal& k) { row[key(SignalKindId::kAccumulate)] = json::object({ { "trigger", "@" + k.trigger.name }, { "decay", ParamToJson(k.decay) } }); },
				[&](const NoiseSignal& k) { row[key(SignalKindId::kNoise)] = NoiseToJson(k); },
				[&](const GradientSignal& k) { row[key(SignalKindId::kGradient)] = GradientToJson(k); },
				[&](const DeltaSignal& k) { row[key(SignalKindId::kDelta)] = "@" + k.of.name; },
				[&](const SmoothSignal& k) { row[key(SignalKindId::kSmooth)] = json::object({ { "of", "@" + k.of.name }, { "seconds", ParamToJson(k.seconds) } }); },
				[&](const ExprSignal& k) { row[key(SignalKindId::kExpr)] = k.text; });
			if (a_signal.curve) {
				row["curve"] = CurveRefToJson(*a_signal.curve);
			}
			return row;
		}

		json ImageToJson(const ImageSource& k)
		{
			json   o = json::object();
			Writer w{ o };
			w.Write("path", k.path);
			w.Write("channel", kImageChannels, k.channel, ImageChannel::kRgb);
			w.Write("space", kImageSpaces, k.space, ImageSpace::kTiled);
			w.Write("scroll", k.scroll);
			w.Write("tile", k.tile);
			if (k.mirror[0] || k.mirror[1]) o["mirror"] = json::array({ k.mirror[0], k.mirror[1] });
			w.Write("transpose", k.transpose, false);
			w.WriteNumber("mip", k.mip, 0.0f);
			return o;
		}

		json BakeToJson(const BakeSource& k)
		{
			return Match(
				k.bake,
				[&](const PartitionBake& p) {
					const auto name = BipedSlotName(p.slot);
					return json::object({ { "partition", name ? json(std::string{ *name }) : json(p.slot) } });
				},
				[&](const BoneWeightBake& b) { return json::object({ { "boneWeight", b.bones } }); },
				[&](const auto&) { return json(std::string{ BakeKindName(k.bake) }); });
		}

		json MaterialClustersToJson(const MaterialClustersSource& k)
		{
			const MaterialClustersSource defaults;
			json                         o = json::object();
			Writer                       w{ o };
			w.Write("clusters", static_cast<std::uint32_t>(k.clusters), static_cast<std::uint32_t>(defaults.clusters));
			json   weights = json::object();
			Writer ww{ weights };
			ww.WriteNumber("roughness", k.roughness, defaults.roughness);
			ww.WriteNumber("metallic", k.metallic, defaults.metallic);
			ww.WriteNumber("occlusion", k.occlusion, defaults.occlusion);
			ww.WriteNumber("reflectance", k.reflectance, defaults.reflectance);
			ww.WriteNumber("luma", k.luma, defaults.luma);
			if (!weights.empty()) o["weights"] = std::move(weights);
			w.Write("seed", k.seed, defaults.seed);
			w.Write("iterations", k.iterations, defaults.iterations);
			return o;
		}

		json SourceToJson(const Source& a_source)
		{
			const std::string word{ SourceKindName(a_source.kind) };
			json              row = json::object();
			Match(
				a_source.kind,
				[&](const ImageSource& k) { row[word] = ImageToJson(k); },
				[&](const MaterialSource& k) { row[word] = NameOf(kMaterialChannels, k.channel); },
				[&](const BakeSource& k) { row[word] = BakeToJson(k); },
				[&](const UvSource& k) { row[word] = NameOf(kUvAxes, k.axis); },
				[&](const DistanceSource& k) {
					Match(
						k.from,
						[&](const std::string& node) { row[word] = node; },
						[&](const Vec3& p) { row[word] = json::object({ { "from", PointToJson(p) } }); });
				},
				[&](const RippleSource& k) {
					json o = json::object({ { "trigger", "@" + k.trigger.name }, { "speed", ParamToJson(k.speed) }, { "width", ParamToJson(k.width) }, { "decay", ParamToJson(k.decay) } });
					if (k.shape != RippleShape::kRing) o["shape"] = NameOf(kRippleShapes, k.shape);
					row[word] = std::move(o);
				},
				[&](const MaterialClustersSource& k) { row[word] = MaterialClustersToJson(k); });
			return row;
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

		json SurfaceOutputToJson(const SurfaceOutput& m)
		{
			json o = json::object();
			o["target"] = NameOf(kSurfaces, m.surface);
			o["slot"] = NameOf(kSlots, m.slot);
			for (const auto& field : kScalarFields) {
				const std::string key{ field.name };
				Match(
					field.member,
					[&](std::optional<Param> SlotScalars::*member) {
						if (m.scalars.*member) o[key] = ParamToJson(*(m.scalars.*member));
					},
					[&](std::optional<Vec3Param> SlotScalars::*member) {
						if (m.scalars.*member) o[key] = VecToJson(*(m.scalars.*member));
					});
			}
			if (!m.selector.All()) o["selector"] = SelectorToJson(m.selector);
			if (m.replace) o["replace"] = true;
			json stack = json::array();
			for (const auto& l : m.stack) {
				stack.push_back(LayerToJson(l));
			}
			o["stack"] = std::move(stack);
			return o;
		}

		json LightOutputToJson(const LightOutput& l)
		{
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
		}

		json OutputToJson(const Output& a_output)
		{
			return Match(
				a_output,
				[](const SurfaceOutput& m) { return SurfaceOutputToJson(m); },
				[](const LightOutput& l) { return LightOutputToJson(l); });
		}

		json ShellToJson(const ShellSettings& a_shell)
		{
			const ShellSettings defaults;
			json                o = json::object();
			Writer              w{ o };
			w.Write("material", kShellMaterials, a_shell.material, defaults.material);
			w.Write("blend", kShellBlends, a_shell.blend, defaults.blend);
			w.Write("depthBias", a_shell.depthBias, defaults.depthBias);
			w.WriteNumber("alphaTest", a_shell.alphaTest, defaults.alphaTest);
			w.Write("alpha", a_shell.alpha, defaults.alpha);
			w.Write("rimPower", a_shell.rimPower, defaults.rimPower);
			w.Write("emissive", a_shell.emissive, defaults.emissive);
			const ShellPose& p = a_shell.pose;
			const ShellPose& d = defaults.pose;
			json             pose = json::object();
			Writer           wp{ pose };
			wp.Write("inflate", p.inflate, d.inflate);
			wp.Write("offset", p.offset, d.offset);
			wp.Write("scale", p.scale, d.scale);
			wp.WritePoint("scalePoint", p.scalePoint, d.scalePoint);
			wp.Write("spin", p.spin, d.spin);
			wp.WritePoint("spinAxis", p.spinAxis, d.spinAxis);
			if (!pose.empty()) o["pose"] = std::move(pose);
			return o;
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
	}

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
