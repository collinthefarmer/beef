#include "recipe/Recipe.h"
#include "recipe/Words.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects
{
	using json = nlohmann::ordered_json;

	namespace
	{
		struct Ctx
		{
			std::vector<Diagnostic>& out;
			std::string              where;

			void Error(std::string a_message) const { out.push_back({ Severity::kError, where, std::move(a_message) }); }
			void Warn(std::string a_message) const { out.push_back({ Severity::kWarning, where, std::move(a_message) }); }
			Ctx  At(std::string a_where) const { return Ctx{ out, std::move(a_where) }; }
		};

		bool RowCapReached(std::size_t a_count, const Ctx& a_ctx, std::string_view a_what)
		{
			if (a_count >= kMaxRecipeRows) {
				a_ctx.Error(std::format("'{}' has more than {} entries", a_what, kMaxRecipeRows));
				return true;
			}
			return false;
		}

		std::size_t MaxNestingDepth(std::string_view a_json) noexcept
		{
			std::size_t depth = 0;
			std::size_t deepest = 0;
			bool        inString = false;
			bool        escaped = false;
			for (const char c : a_json) {
				if (inString) {
					if (escaped) {
						escaped = false;
					} else if (c == '\\') {
						escaped = true;
					} else if (c == '"') {
						inString = false;
					}
					continue;
				}
				if (c == '"') {
					inString = true;
				} else if (c == '{' || c == '[') {
					++depth;
					deepest = std::max(deepest, depth);
				} else if ((c == '}' || c == ']') && depth > 0) {
					--depth;
				}
			}
			return deepest;
		}

		class Reader
		{
		public:
			Reader(const json& a_object, Ctx a_ctx) :
				object_(a_object), ctx_(std::move(a_ctx)) {}

			[[nodiscard]] const Ctx& Context() const noexcept { return ctx_; }
			[[nodiscard]] bool       Has(std::string_view a_key) const { return object_.is_object() && object_.contains(a_key); }

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

			template <class Row, std::size_t N>
			std::optional<decltype(Row::value)> Enum(std::string_view a_key, const Row (&a_table)[N])
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
				for (std::size_t i = 0; i < N; ++i) {
					const auto p = ParamFrom(a_j[i], a_what);
					if (!p) {
						return std::nullopt;
					}
					parts[i] = *p;
				}
				if constexpr (N == 3) {
					if (a_color) {
						NormaliseColor(parts);
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
					return Value{ Vec3{ a_j[0].get<float>(), a_j[1].get<float>(), a_j[2].get<float>() } };
				}
				a_ctx.Error(std::format("'{}' must be a number, [x, y] or [r, g, b]", a_what));
				return std::nullopt;
			}

		private:
			const json&                     object_;
			Ctx                             ctx_;
			std::unordered_set<std::string> used_;
		};

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

		std::optional<RecipeKey> KeyFrom(const json& a_j, const Ctx& a_ctx)
		{
			if (a_j.is_string()) {
				if (a_j.get<std::string>() == "default") {
					return RecipeKey{ KeyKind::kDefault };
				}
				a_ctx.Error(std::format("a key is \"default\" or {{\"<kind>\": ...}}; got '{}'", a_j.get<std::string>()));
				return std::nullopt;
			}
			const auto entry = OneKey(a_j, a_ctx, "a key");
			if (!entry) {
				return std::nullopt;
			}
			const auto  kind = FromName(kKeyKinds, entry->key);
			const auto* row = kind ? RowOf(kKeyKinds, *kind) : nullptr;
			if (!row || row->operand == KeyOperand::kNone) {
				a_ctx.Error(std::format("unknown key kind '{}'; one of {}", entry->key, Choices(kKeyKinds)));
				return std::nullopt;
			}
			RecipeKey key;
			key.kind = row->value;
			if (row->operand == KeyOperand::kGlob) {
				const auto glob = GlobFrom(*entry->value, a_ctx, entry->key);
				if (!glob) {
					return std::nullopt;
				}
				key.operand = *glob;
				return key;
			}
			const auto form = FormFrom(*entry->value, a_ctx, entry->key);
			if (!form) {
				return std::nullopt;
			}
			key.operand = *form;
			return key;
		}

		Selector SelectorFrom(const json& a_j, const Ctx& a_ctx)
		{
			Selector s;
			if (!a_j.is_array()) {
				a_ctx.Error("'selector' must be an array of {\"kind\": ...} terms");
				return s;
			}
			for (const auto& e : a_j) {
				if (RowCapReached(s.anyOf.size(), a_ctx, "selector")) {
					break;
				}
				const auto entry = OneKey(e, a_ctx, "a selector term");
				if (!entry) {
					continue;
				}
				const auto kind = FromName(kSelectorKinds, entry->key);
				if (!kind) {
					a_ctx.Error(std::format("unknown selector kind '{}'; one of {}", entry->key, Choices(kSelectorKinds)));
					continue;
				}
				SelectorClause term;
				term.kind = *kind;
				if (*kind == SelectorKind::kAddon) {
					const auto form = FormFrom(*entry->value, a_ctx, entry->key);
					if (!form) {
						continue;
					}
					term.operand = *form;
				} else {
					const auto glob = GlobFrom(*entry->value, a_ctx, entry->key);
					if (!glob) {
						continue;
					}
					term.operand = *glob;
				}
				s.anyOf.push_back(std::move(term));
			}
			return s;
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
			const auto kindId = ParseSignalKind(kind);
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

			if (kindId == SignalKindId::kConstant) {
				const auto value = Reader::ValueFrom(v, kind, a_ctx);
				if (!value) {
					return std::nullopt;
				}
				s.kind = ConstantSignal{ *value };
			} else if (kindId == SignalKindId::kPulse) {
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
			} else if (kindId == SignalKindId::kRamp) {
				RampSignal k;
				if (!object([&](Reader& r) {
						if (auto p = r.Parameter("from")) k.from = *p;
						if (auto p = r.Parameter("to")) k.to = *p;
						if (auto p = r.Parameter("seconds")) k.seconds = *p;
					})) {
					return std::nullopt;
				}
				s.kind = k;
			} else if (kindId == SignalKindId::kEfsh) {
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
			} else if (kindId == SignalKindId::kActorValue) {
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
			} else if (kindId == SignalKindId::kActorState) {
				const auto state = v.is_string() ? FromName(kActorStates, v.get<std::string>()) : std::nullopt;
				if (!state) {
					a_ctx.Error(std::format("'actorState' is one of {}", Choices(kActorStates)));
					return std::nullopt;
				}
				s.kind = ActorStateSignal{ *state };
			} else if (kindId == SignalKindId::kEnchantment) {
				const auto field = v.is_string() ? FromName(kEnchantmentFields, v.get<std::string>()) : std::nullopt;
				if (!field) {
					a_ctx.Error(std::format("'enchantment' is one of {}", Choices(kEnchantmentFields)));
					return std::nullopt;
				}
				s.kind = EnchantmentSignal{ *field };
			} else if (kindId == SignalKindId::kTrigger) {
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
					EventOrigin es;
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
					k.origin = es;
				} else if (source->key == "plugin") {
					if (!source->value->is_string() || source->value->get<std::string>().empty()) {
						a_ctx.Error("'plugin' is an id string");
						return std::nullopt;
					}
					if (r.Has("filter") || r.Has("at") || r.Has("value")) a_ctx.Error("'filter', 'at' and 'value' belong to 'event' or 'when' triggers");
					k.origin = PluginOrigin{ source->value->get<std::string>() };
				} else if (source->key == "when") {
					WhenOrigin ws;
					const auto when = r.RefFrom(*source->value, "when");
					if (!when) {
						return std::nullopt;
					}
					ws.when = *when;
					ws.value = r.Reference("value");
					if (r.Has("filter") || r.Has("at")) a_ctx.Error("'filter' and 'at' belong to 'event' triggers");
					k.origin = ws;
				} else {
					a_ctx.Error(std::format("a trigger's source is 'event', 'plugin' or 'when', not '{}'", source->key));
					return std::nullopt;
				}
				r.Finish();
				s.kind = k;
			} else if (kindId == SignalKindId::kPayload) {
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
			} else if (kindId == SignalKindId::kCounter) {
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
			} else if (kindId == SignalKindId::kAccumulate) {
				AccumulateSignal k;
				if (!object([&](Reader& r) {
						if (auto t = r.Reference("trigger")) k.trigger = *t;
						else if (!r.Has("trigger")) a_ctx.Error("'accumulate' needs 'trigger'");
						if (auto p = r.Parameter("decay")) k.decay = *p;
					})) {
					return std::nullopt;
				}
				s.kind = k;
			} else if (kindId == SignalKindId::kNoise) {
				NoiseSignal k;
				if (!object([&](Reader& r) {
						if (auto p = r.Parameter("frequency")) k.frequency = *p;
						if (auto p = r.Parameter("amplitude")) k.amplitude = *p;
						if (auto seed = r.Integer("seed")) k.seed = static_cast<std::uint32_t>(std::max(0, *seed));
					})) {
					return std::nullopt;
				}
				s.kind = k;
			} else if (kindId == SignalKindId::kGradient) {
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
							if (RowCapReached(k.stops.size(), a_ctx, "stops")) {
								break;
							}
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
			} else if (kindId == SignalKindId::kDelta) {
				const auto of = row.RefFrom(v, kind);
				if (!of) {
					return std::nullopt;
				}
				s.kind = DeltaSignal{ *of };
			} else if (kindId == SignalKindId::kSmooth) {
				SmoothSignal k;
				if (!object([&](Reader& r) {
						if (auto of = r.Reference("of")) k.of = *of;
						else if (!r.Has("of")) a_ctx.Error("'smooth' needs 'of'");
						if (auto p = r.Parameter("seconds")) k.seconds = *p;
					})) {
					return std::nullopt;
				}
				s.kind = k;
			} else if (kindId == SignalKindId::kExpr) {
				if (!v.is_string() || v.get<std::string>().empty()) {
					a_ctx.Error("'expr' is an expression string");
					return std::nullopt;
				}
				s.kind = ExprSignal{ v.get<std::string>() };
			} else {
				a_ctx.Error(std::format("unknown signal kind '{}'; one of {}", kind, Choices(kSignalKinds)));
				return std::nullopt;
			}
			s.curve = CurveRefFrom(row);
			row.Finish();
			return s;
		}

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
			const auto  blank = DefaultSourceKind(kind);
			if (!blank) {
				a_ctx.Error(std::format("unknown source kind '{}'", kind));
				return std::nullopt;
			}
			if (Is<ImageSource>(*blank)) {
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
			} else if (Is<MaterialSource>(*blank)) {
				const auto channel = v.is_string() ? FromName(kMaterialChannels, v.get<std::string>()) : std::nullopt;
				if (!channel) {
					a_ctx.Error(std::format("'material' is one of {}", Choices(kMaterialChannels)));
					return std::nullopt;
				}
				s.kind = MaterialSource{ *channel };
			} else if (Is<BakeSource>(*blank)) {
				BakeSource k;
				if (v.is_string()) {
					const auto bare = DefaultBakeKind(v.get<std::string>());
					if (!bare || Is<PartitionBake>(*bare) || Is<BoneWeightBake>(*bare)) {
						a_ctx.Error(std::format("'bake' is one of {}, or {{\"partition\": slot}} or {{\"boneWeight\": [bones]}}", Choices(kBakeKindWords)));
						return std::nullopt;
					}
					k.bake = *bare;
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
							if (RowCapReached(bw.bones.size(), a_ctx, "boneWeight")) {
								break;
							}
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
			} else if (Is<UvSource>(*blank)) {
				const auto axis = v.is_string() ? FromName(kUvAxes, v.get<std::string>()) : std::nullopt;
				if (!axis) {
					a_ctx.Error("'uv' is \"u\" or \"v\"");
					return std::nullopt;
				}
				s.kind = UvSource{ *axis };
			} else if (Is<DistanceSource>(*blank)) {
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
			} else if (Is<RippleSource>(*blank)) {
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
				if (!v.is_object()) {
					a_ctx.Error("'materialClusters' takes an object with 'clusters', 'weights', 'seed' and 'iterations'");
					return std::nullopt;
				}
				MaterialClustersSource k;
				Reader                 r(v, a_ctx);
				bool                   inRange = true;
				if (auto n = r.Integer("clusters")) {
					if (*n < 1 || *n > kMaxMaterialClusters) {
						a_ctx.Error(std::format("'clusters' is 1..{}", kMaxMaterialClusters));
						inRange = false;
					} else {
						k.clusters = static_cast<std::uint8_t>(*n);
					}
				}
				if (const auto* w = r.Child("weights")) {
					if (!w->is_object()) {
						a_ctx.Error("'weights' is an object of roughness, metallic, occlusion, reflectance and luma");
						inRange = false;
					} else {
						Reader wr(*w, a_ctx);
						for (const auto& [field, weight] : { std::pair{ "roughness", &k.roughness }, std::pair{ "metallic", &k.metallic }, std::pair{ "occlusion", &k.occlusion }, std::pair{ "reflectance", &k.reflectance }, std::pair{ "luma", &k.luma } }) {
							if (auto x = wr.Number(field)) {
								if (!(*x >= 0.0f && *x <= kMaxChannelWeight)) {
									a_ctx.Error(std::format("'weights.{}' is 0..{}", field, kMaxChannelWeight));
									inRange = false;
								} else {
									*weight = *x;
								}
							}
						}
						wr.Finish();
					}
				}
				if (auto n = r.Integer("seed")) {
					if (*n < 0) {
						a_ctx.Error("'seed' is a whole number");
						inRange = false;
					} else {
						k.seed = static_cast<std::uint32_t>(*n);
					}
				}
				if (auto n = r.Integer("iterations")) {
					if (*n < 1 || *n > static_cast<int>(kMaxClusterIterations)) {
						a_ctx.Error(std::format("'iterations' is 1..{}", kMaxClusterIterations));
						inRange = false;
					} else {
						k.iterations = static_cast<std::uint32_t>(*n);
					}
				}
				r.Finish();
				if (!inRange) {
					return std::nullopt;
				}
				s.kind = k;
			}
			return s;
		}

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
					const Vec3           raw = *Get<Vec3>(*value);
					std::array<Param, 3> parts{ raw.x, raw.y, raw.z };
					NormaliseColor(parts);
					l.source = Vec3{ *Get<float>(parts[0]), *Get<float>(parts[1]), *Get<float>(parts[2]) };
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
								if (RowCapReached(nb.bones.size(), a_ctx, "named")) {
									break;
								}
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
			SurfaceOutput m;
			m.surface = *surface;
			if (auto slot = r.Enum("slot", kSlots)) m.slot = *slot;
			else if (!r.Has("slot")) a_ctx.Error("'slot' is required");
			for (const auto& field : kScalarFields) {
				Match(
					field.member,
					[&](std::optional<Param> SlotScalars::*member) { m.scalars.*member = r.Parameter(field.name); },
					[&](std::optional<Vec3Param> SlotScalars::*member) { m.scalars.*member = r.Vector3(field.name, true); });
			}
			if (const auto* sel = r.Child("selector")) m.selector = SelectorFrom(*sel, a_ctx);
			if (auto b = r.Boolean("replace")) m.replace = *b;
			if (const auto* stack = r.Child("stack")) {
				if (!stack->is_array()) {
					a_ctx.Error("'stack' must be an array of layers");
				} else {
					std::size_t i = 0;
					for (const auto& layer : *stack) {
						if (RowCapReached(m.stack.size(), a_ctx, "stack")) {
							break;
						}
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
						if (RowCapReached(v.overrides.size(), ctx, "overrides")) {
							break;
						}
						if (auto val = Reader::ValueFrom(value, name, ctx)) {
							v.overrides[name] = *val;
						}
					}
				}
			}
			r.Finish();
			return v;
		}

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
				if (RowCapReached(a_out.size(), a_root.Context(), a_section)) {
					break;
				}
				if (auto row = a_parse(name, value, a_root.Context().At(std::format("{} {}", a_rowWord, name)))) {
					a_out.push_back(std::move(*row));
				}
			}
		}

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

	LoadResult ParseRecipe(std::string_view a_json, std::string_view a_id)
	{
		LoadResult result;
		const Ctx  fileCtx{ result.diagnostics, "file" };

		if (MaxNestingDepth(a_json) > kMaxRecipeDepth) {
			fileCtx.Error(std::format("nested deeper than {} levels", kMaxRecipeDepth));
			return result;
		}

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
					if (RowCapReached(recipe.keys.size(), ctx, "keys")) {
						break;
					}
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
					if (RowCapReached(recipe.outputs.size(), ctx, "outputs")) {
						break;
					}
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
					if (RowCapReached(recipe.variants.size(), ctx, "variants")) {
						break;
					}
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
}
