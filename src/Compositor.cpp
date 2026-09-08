#include "Compositor.h"

#include "Analysis.h"

#include "Expression.h"
#include "MeshReader.h"
#include "RecipeStore.h"
#include "Settings.h"

#include <cctype>
#include <array>

namespace WornEnchantmentPBR
{
	namespace
	{
		std::string Lower(std::string_view a_text)
		{
			std::string out{ a_text };
			for (auto& c : out) {
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return out;
		}

		// The engine's placeholder textures are 1x1; a real map is larger. The size
		// comes from the D3D resource: the renderer record says 0x0 for streamed maps.
		bool RealTexture(const RE::NiPointer<RE::NiSourceTexture>& a_texture)
		{
			const auto extent = TextureLab::ExtentOf(a_texture.get());
			return extent && extent->width > 4 && extent->height > 4;
		}

		// Why RealTexture rejected a map, for the diagnostic on the row.
		std::string DescribeTexture(const RE::NiPointer<RE::NiSourceTexture>& a_texture)
		{
			if (!a_texture) {
				return "the material has no texture in this slot";
			}
			const auto* data = reinterpret_cast<const RE::NiTexture::RendererData*>(a_texture->rendererTexture);
			const char* name = a_texture->name.c_str() ? a_texture->name.c_str() : "";
			if (!data) {
				return std::format("'{}' is not resident (no renderer data)", name);
			}
			if (!data->resourceView) {
				return std::format("'{}' has no shader resource view", name);
			}
			const auto extent = TextureLab::ExtentOf(a_texture.get());
			if (!extent) {
				return std::format("'{}' is not a 2D texture", name);
			}
			return std::format("'{}' is {}x{}, a placeholder", name, extent->width, extent->height);
		}

		std::uint32_t ChannelBits(const ChannelSet& a_set)
		{
			return (a_set.r ? 1u : 0u) | (a_set.g ? 2u : 0u) | (a_set.b ? 4u : 0u) | (a_set.a ? 8u : 0u);
		}

		// Which material texture and channel a material source reads.
		struct MaterialChannelPick
		{
			RE::NiPointer<RE::NiSourceTexture> texture;
			ShaderChannel                      channel = ShaderChannel::kRgb;
			std::string                        problem;
		};

		// One of the material's maps; null for kNone.
		RE::NiPointer<RE::NiSourceTexture> MapOf(MaterialMap a_map, const MaterialInputs& a_material)
		{
			switch (a_map) {
			case MaterialMap::kDiffuse:
				return a_material.diffuse;
			case MaterialMap::kNormal:
				return a_material.normal;
			case MaterialMap::kRmaos:
				return a_material.rmaos;
			case MaterialMap::kDisplacement:
				return a_material.displacement;
			case MaterialMap::kNone:
				return nullptr;
			}
			return nullptr;
		}

		// The map a slot edits in place (BaseMapOf), as the material holds it.
		RE::NiPointer<RE::NiSourceTexture> BaseMapFor(Slot a_slot, const MaterialInputs& a_material)
		{
			return MapOf(BaseMapOf(a_slot), a_material);
		}

		// Many PBR sets ship a displacement map that is a real texture and
		// entirely black (NOTES 46): flat when its mean sits at either end.
		// A readback, so once per material in MaterialInputs::From.
		bool MeasureFlatDisplacement(const RE::NiPointer<RE::NiSourceTexture>& a_displacement)
		{
			if (!RealTexture(a_displacement)) {
				return true;
			}
			const float mean = TextureLab::GetSingleton()->MeanChannel(a_displacement.get(), ShaderChannel::kR);
			return !(mean > 0.02f && mean < 0.98f);
		}

		std::optional<MaterialChannel> SingleChannelOf(const Recipe& a_recipe, const Mask& a_mask);

		// The normal map's slope as a texture of its own, through the lab's
		// channel pass with slope set; null with the reason when the map is
		// not real or the pass fails.
		std::shared_ptr<TextureLab::Target> RenderNormalSlope(const MaterialInputs& a_material, std::string& a_problem)
		{
			if (!RealTexture(a_material.normal)) {
				a_problem = "normal map: " + DescribeTexture(a_material.normal);
				return nullptr;
			}
			const auto extent = TextureLab::ExtentOf(a_material.normal.get());
			auto*      lab = TextureLab::GetSingleton();
			auto       target = lab->Acquire(TextureSize::Clamp(extent ? (std::max)(extent->width, extent->height) : 0));
			if (!target) {
				a_problem = "no render target for the normal slope";
				return nullptr;
			}
			TextureLab::LayerParams params;
			params.mode = TextureLab::Mode::kChannel;
			params.armor = { a_material.normal.get(), TextureLab::ArmorInput::kNormalSlope };
			params.channel.slope = true;
			if (!lab->Render(*target, nullptr, params)) {
				a_problem = "the normal slope pass failed";
				return nullptr;
			}
			return target;
		}

		// The material's cluster map under a_settings as a texture of its own,
		// stored on the geometry's derived maps: the apply's analysis when the
		// settings are its own, else the stored sample clustered again on the
		// CPU (no readback), then the lab's classify pass into a target sized
		// like the RMAOS map. The map already there is returned when its
		// settings match; other settings replace it, so a source that reads
		// it must hold the returned target for as long as it samples it.
		// Null with derived.clustersProblem set when the material was not
		// sampled or the pass fails. Game thread, from PrepareSource;
		// InspectSource reads derived.clusters alone.
		std::shared_ptr<TextureLab::Target> RenderClusterMap(const GeometryInputs& a_inputs, const ClusterSettings& a_settings)
		{
			const MaterialInputs& material = a_inputs.material;
			DerivedMaps&          derived = *a_inputs.derived;
			if (derived.clusters && derived.clusterSettings == a_settings) {
				return derived.clusters;
			}
			derived.clustersTried = true;
			derived.clusters = nullptr;
			derived.clusterSettings = a_settings;
			derived.clustersProblem.clear();
			const auto& record = Compositor::GetSingleton()->AnalyseMaterial(material);
			if (!record.sample || !record.analysis) {
				derived.clustersProblem = record.problem.empty() ? "the material could not be sampled" : record.problem;
				return nullptr;
			}
			const bool             defaults = record.analysis->settings == a_settings;
			const MaterialAnalysis analysis = defaults ? *record.analysis : ClusterMaterial(*record.sample, a_settings);
			if (analysis.clusters.empty()) {
				derived.clustersProblem = "the material sample clustered into nothing";
				return nullptr;
			}
			const auto extent = TextureLab::ExtentOf(material.rmaos.get());
			auto*      lab = TextureLab::GetSingleton();
			auto       target = lab->Acquire(TextureSize::Clamp(extent ? (std::max)(extent->width, extent->height) : 0));
			if (!target) {
				derived.clustersProblem = "no render target for the cluster map";
				return nullptr;
			}
			if (!lab->RenderClusters(*target, material.rmaos.get(), material.diffuse.get(), analysis)) {
				derived.clustersProblem = lab->ClassifyAvailable() ? "the classify pass failed" : "the classify pass is unavailable";
				return nullptr;
			}
			derived.clusters = target;
			return target;
		}

		// With a_mayRender the slope is rendered on first use (the prepare
		// paths, game thread); an inspection only reads what was rendered.
		// A channel with a map reads that map's channel from the table; the
		// two derived channels are decided here.
		MaterialChannelPick PickMaterialChannel(MaterialChannel a_channel, const GeometryInputs& a_inputs, bool a_mayRender)
		{
			const MaterialInputs& a_material = a_inputs.material;
			const auto            map = MaterialMapOf(a_channel);
			if (map != MaterialMap::kNone) {
				return { MapOf(map, a_material), ShaderChannelOf(a_channel), {} };
			}
			switch (a_channel) {
			case MaterialChannel::kRelief:
				// A flat height map carries no relief; the occlusion channel does.
				if (!a_material.flatDisplacement) {
					return { a_material.displacement, ShaderChannelOf(MaterialChannel::kDisplacement), {} };
				}
				return { a_material.rmaos, ShaderChannelOf(MaterialChannel::kOcclusion), {} };
			case MaterialChannel::kNormalSlope: {
				auto& derived = *a_inputs.derived;
				if (!derived.normalSlope && a_mayRender && !derived.tried) {
					derived.tried = true;
					derived.normalSlope = RenderNormalSlope(a_material, derived.problem);
				}
				if (derived.normalSlope) {
					return { RE::NiPointer<RE::NiSourceTexture>{ derived.normalSlope->Texture() }, ShaderChannelOf(a_channel), {} };
				}
				return { nullptr, ShaderChannelOf(a_channel), derived.tried ? derived.problem : std::string{ Compositor::kNotRendered } };
			}
			default:
				return { nullptr, ShaderChannel::kR, "unknown channel" };
			}
		}
	}

	MaterialInputs MaterialInputs::From(const PBRMaterialLayout& a_material)
	{
		MaterialInputs in;
		in.diffuse = a_material.diffuseTexture;
		in.normal = a_material.normalTexture;
		in.rmaos = a_material.rmaosTexture;
		in.displacement = a_material.displacementTexture;
		in.flatDisplacement = MeasureFlatDisplacement(in.displacement);
		return in;
	}

	RE::NiSourceTexture* RenderedStack::Texture() const noexcept
	{
		return latest_ ? latest_->Texture() : nullptr;
	}

	Compositor* Compositor::GetSingleton()
	{
		static Compositor compositor;
		return &compositor;
	}

	RE::NiPointer<RE::NiSourceTexture> Compositor::LoadImage(std::string_view a_path)
	{
		if (a_path.empty()) {
			return nullptr;
		}
		const auto key = Lower(a_path);
		if (const auto it = images_.find(key); it != images_.end()) {
			return it->second;
		}
		// The engine takes the record's raw path and the textures\ prefixed form (NOTES 7).
		RE::NiPointer<RE::NiTexture> texture;
		RE::BSShaderManager::GetTexture(std::string{ a_path }.c_str(), true, texture, false);
		auto* source = texture ? netimmerse_cast<RE::NiSourceTexture*>(texture.get()) : nullptr;
		if (!source || !source->rendererTexture) {
			const auto prefixed = "textures\\" + std::string{ a_path };
			RE::BSShaderManager::GetTexture(prefixed.c_str(), true, texture, false);
			source = texture ? netimmerse_cast<RE::NiSourceTexture*>(texture.get()) : nullptr;
		}
		RE::NiPointer<RE::NiSourceTexture> result{ source && source->rendererTexture ? source : nullptr };
		images_[key] = result;
		return result;
	}

	std::optional<PreparedSource> Compositor::PrepareSource(const Recipe& a_recipe, const Ref& a_ref, const GeometryInputs& a_inputs, TextureSize a_size, std::vector<Diagnostic>& a_out, const std::string& a_where, std::uint32_t a_depth)
	{
		if (a_recipe.FindMask(a_ref.name)) {
			// A mask read as a source: its rendered target, by mesh UV.
			auto rendered = PrepareRenderedMask(a_recipe, a_ref.name, a_inputs, a_size, a_depth);
			PreparedSource prepared;
			if (!rendered || !rendered->Problem().empty()) {
				prepared.problem = rendered ? rendered->Problem() : "mask could not be prepared";
				a_out.push_back({ Severity::kWarning, a_where, std::format("'@{}': {}; the layer is skipped", a_ref.name, prepared.problem) });
				return prepared;
			}
			prepared.texture = RE::NiPointer<RE::NiSourceTexture>{ rendered->Texture() };
			prepared.sampling.channel = rendered->Vector() ? ShaderChannel::kRgb : ShaderChannel::kR;
			prepared.sampling.meshSpace = true;
			prepared.animated = rendered->Animated();
			prepared.rendered = std::move(rendered);
			return prepared;
		}
		const auto* source = a_recipe.FindSource(a_ref.name);
		if (!source) {
			a_out.push_back({ Severity::kError, a_where, std::format("source '@{}' not found", a_ref.name) });
			return std::nullopt;
		}
		PreparedSource prepared;
		prepared.animated = IsAnimated(a_recipe, *source);
		Match(
			source->kind,
			[&](const ImageSource& image) {
				prepared.texture = LoadImage(image.path);
				if (!prepared.texture) {
					prepared.problem = std::format("image '{}' did not load", image.path);
				}
				prepared.sampling.channel = ShaderChannelOf(image.channel);
				prepared.sampling.meshSpace = image.space == ImageSpace::kMesh;
				prepared.sampling.transform.mirrorU = image.mirror[0];
				prepared.sampling.transform.mirrorV = image.mirror[1];
				prepared.sampling.transform.transpose = image.transpose;
				prepared.sampling.transform.sourceMip = image.mip;
				prepared.scroll = image.scroll;
				prepared.tile = image.tile;
				// A colour field is normalised by its mean luminance; mask data is not.
				if (prepared.texture && image.channel == ImageChannel::kRgb) {
					const float mean = TextureLab::GetSingleton()->MeanLuminance(prepared.texture.get());
					prepared.normalize = 0.5f / std::max(mean, 0.05f);
				}
			},
			[&](const MaterialSource& material) {
				auto pick = PickMaterialChannel(material.channel, a_inputs, true);
				prepared.texture = pick.texture;
				prepared.sampling.channel = pick.channel;
				prepared.sampling.meshSpace = true;
				prepared.problem = pick.problem;
				if (prepared.problem.empty() && !RealTexture(prepared.texture)) {
					prepared.problem = DescribeTexture(prepared.texture);
				}
			},
			[&](const BakeSource& bake) {
				auto target = PrepareBake(bake, a_inputs, a_size);
				if (!target) {
					prepared.problem = target.error();
					return;
				}
				prepared.texture = RE::NiPointer<RE::NiSourceTexture>{ (*target)->Texture() };
				prepared.sampling.channel = Is<PositionBake>(bake.bake) || Is<LocalPositionBake>(bake.bake) ? ShaderChannel::kRgb : ShaderChannel::kR;
				prepared.sampling.meshSpace = true;
			},
			[&](const DistanceSource& distance) {
				auto target = PrepareDistance(distance, a_inputs, a_size);
				if (!target) {
					prepared.problem = target.error();
					return;
				}
				prepared.texture = RE::NiPointer<RE::NiSourceTexture>{ (*target)->Texture() };
				prepared.sampling.channel = ShaderChannel::kR;
				prepared.sampling.meshSpace = true;
			},
			[&](const RippleSource& ripple) {
				auto rendered = PrepareRipple(*source, ripple, a_inputs, a_size);
				if (!rendered) {
					prepared.problem = rendered.error();
					return;
				}
				prepared.texture = RE::NiPointer<RE::NiSourceTexture>{ (*rendered)->Texture() };
				prepared.sampling.channel = ShaderChannel::kR;
				prepared.sampling.meshSpace = true;
				prepared.animated = true;
				prepared.ripple = std::move(*rendered);
			},
			[&](const UvSource& uv) {
				const auto entry = MeshOf(a_inputs.geometry.get());
				if (!entry) {
					prepared.problem = entry.error();
					return;
				}
				auto target = BakeInto(**entry, UvKeyOf(uv.axis, a_size), a_size, [&] { return BuildUvBake(*(*entry)->mesh, uv.axis); });
				if (!target) {
					prepared.problem = target.error();
					return;
				}
				prepared.texture = RE::NiPointer<RE::NiSourceTexture>{ (*target)->Texture() };
				prepared.sampling.channel = ShaderChannel::kR;
				prepared.sampling.meshSpace = true;
			},
			[&](const MaterialClustersSource& clusters) {
				const auto target = RenderClusterMap(a_inputs, SettingsOf(clusters));
				if (!target) {
					prepared.problem = a_inputs.derived->clustersProblem;
					return;
				}
				prepared.texture = RE::NiPointer<RE::NiSourceTexture>{ target->Texture() };
				prepared.sampling.channel = ShaderChannel::kR;
				prepared.sampling.meshSpace = true;
			});
		if (!prepared.problem.empty()) {
			a_out.push_back({ Severity::kWarning, a_where, std::format("'@{}': {}; the layer is skipped", a_ref.name, prepared.problem) });
		}
		return prepared;
	}

	// A mask that is exactly one material channel reads the map directly;
	// any other expression renders through the interpreter pass.
	std::optional<PreparedMask> Compositor::PrepareMask(const Recipe& a_recipe, const Ref& a_ref, const GeometryInputs& a_inputs, TextureSize a_size, std::vector<Diagnostic>& a_out, const std::string& a_where)
	{
		const auto* mask = a_recipe.FindMask(a_ref.name);
		if (!mask) {
			a_out.push_back({ Severity::kError, a_where, std::format("mask '@{}' not found", a_ref.name) });
			return std::nullopt;
		}
		PreparedMask prepared;
		prepared.animated = IsAnimated(a_recipe, *mask);
		if (const auto channel = SingleChannelOf(a_recipe, *mask)) {
			auto pick = PickMaterialChannel(*channel, a_inputs, true);
			if (!pick.problem.empty() || !RealTexture(pick.texture)) {
				prepared.problem = pick.problem.empty() ? DescribeTexture(pick.texture) + "; evaluates as white" : pick.problem;
				a_out.push_back({ Severity::kWarning, a_where, std::format("mask '@{}': {}", a_ref.name, prepared.problem) });
				return prepared;
			}
			prepared.texture = pick.texture;
			prepared.channel = pick.channel;
			return prepared;
		}
		auto rendered = PrepareRenderedMask(a_recipe, a_ref.name, a_inputs, a_size, 0);
		if (!rendered || !rendered->Problem().empty()) {
			prepared.problem = (rendered ? rendered->Problem() : "mask could not be prepared") + "; evaluates as white";
			a_out.push_back({ Severity::kWarning, a_where, std::format("mask '@{}': {}", a_ref.name, prepared.problem) });
			return prepared;
		}
		prepared.texture = RE::NiPointer<RE::NiSourceTexture>{ rendered->Texture() };
		prepared.channel = ShaderChannel::kR;
		prepared.animated = rendered->Animated();
		prepared.rendered = std::move(rendered);
		return prepared;
	}

	// A layer's curve as a lookup over the source value. The curve's `mean` is
	// the source's own mean, so "x - mean" centres on the map's average.
	std::shared_ptr<TextureLab::Lookup> Compositor::BakeCurve(const Recipe& a_recipe, const CurveRef& a_curve, const std::optional<PreparedSource>& a_source, std::vector<Diagnostic>& a_out, const std::string& a_where)
	{
		std::string text = a_curve.text;
		if (const auto name = a_curve.Named()) {
			const auto* curve = a_recipe.FindCurve(*name);
			if (!curve) {
				a_out.push_back({ Severity::kError, a_where, std::format("curve '@{}' not found; ignored", *name) });
				return nullptr;
			}
			text = curve->text;
		}
		const auto program = ParseCurve(text);
		if (!program) {
			a_out.push_back({ Severity::kError, a_where, std::format("curve: {}; ignored", program.error()) });
			return nullptr;
		}
		float mean = 0.5f;
		if (a_source && a_source->texture) {
			auto* lab = TextureLab::GetSingleton();
			const auto channel = a_source->sampling.channel;
			mean = channel == ShaderChannel::kRgb || channel == ShaderChannel::kLuma ? lab->MeanLuminance(a_source->texture.get()) : lab->MeanChannel(a_source->texture.get(), channel);
		}
		std::array<float, 256> values{};
		for (std::size_t i = 0; i < values.size(); ++i) {
			values[i] = ApplyCurve(*program, static_cast<float>(i) / 255.0f, mean);
		}
		auto lookup = TextureLab::GetSingleton()->CreateLookup(values);
		if (!lookup) {
			a_out.push_back({ Severity::kWarning, a_where, "curve lookup could not be created; ignored" });
		}
		return lookup;
	}

	std::shared_ptr<TextureLab::Target> Compositor::NeutralHeight()
	{
		if (neutralHeight_ && neutralHeight_->Texture()) {
			return neutralHeight_;
		}
		auto* lab = TextureLab::GetSingleton();
		auto  target = lab->Acquire(TextureSize::Clamp(64));
		if (!target) {
			return nullptr;
		}
		TextureLab::LayerParams params;
		params.mode = TextureLab::Mode::kLayer;
		params.layer.color[0] = params.layer.color[1] = params.layer.color[2] = 0.5f;
		params.layer.opacity = 1.0f;
		params.layer.blend = 0;
		if (!lab->Render(*target, nullptr, params)) {
			return nullptr;
		}
		neutralHeight_ = std::move(target);
		return neutralHeight_;
	}

	namespace
	{
		// The size a stack that edits an existing map renders at: the map's
		// own resolution, never below a_size and never above a_maxSize (or
		// a_size when a_maxSize is the smaller).
		TextureSize SizeOverBase(TextureSize a_size, TextureSize a_maxSize, std::uint32_t a_largestSide)
		{
			const std::uint32_t floor = a_size.Pixels();
			const std::uint32_t ceiling = std::max(a_maxSize.Pixels(), floor);
			return TextureSize::Clamp(std::clamp(a_largestSide, floor, ceiling));
		}
	}

	std::unique_ptr<RenderedStack> Compositor::Prepare(const Recipe& a_recipe, const SurfaceOutput& a_output, const GeometryInputs& a_inputs, TextureSize a_size, TextureSize a_maxSize)
	{
		const auto& a_material = a_inputs.material;
		auto* lab = TextureLab::GetSingleton();
		if (!lab->Init()) {
			return nullptr;
		}
		auto stack = std::make_unique<RenderedStack>();
		stack->size_ = a_size;
		if (a_output.slot == Slot::kHeight && a_material.flatDisplacement) {
			// A flat displacement map would put every texel outside a mask at
			// (0 - 0.5) * scale; the neutral base leaves them where they are.
			stack->neutral_ = NeutralHeight();
			if (stack->neutral_ && stack->neutral_->Texture()) {
				stack->base_ = RE::NiPointer{ stack->neutral_->Texture() };
			} else {
				stack->diagnostics_.push_back({ Severity::kWarning, "stack", "the neutral height base could not be rendered; the stack starts from black" });
			}
		} else if (const auto base = BaseMapFor(a_output.slot, a_material); RealTexture(base)) {
			stack->base_ = base;
			const auto extent = TextureLab::ExtentOf(base.get());
			const auto largest = extent ? std::max(extent->width, extent->height) : 0u;
			stack->size_ = SizeOverBase(a_size, a_maxSize, largest);
		}
		stack->animated_ = IsAnimated(a_recipe, Output{ a_output });
		const TextureSize size = stack->size_;
		std::size_t index = 0;
		for (const auto& layer : a_output.stack) {
			const auto    where = std::format("layer {}", index);
			PreparedLayer prepared;
			prepared.layer = &layer;
			prepared.index = index++;
			if (const auto* ref = Get<Ref>(layer.source)) {
				prepared.source = PrepareSource(a_recipe, *ref, a_inputs, size, stack->diagnostics_, where);
				if (!prepared.source || !prepared.source->problem.empty()) {
					continue;
				}
			}
			if (layer.curve) {
				prepared.curve = BakeCurve(a_recipe, *layer.curve, prepared.source, stack->diagnostics_, where);
			}
			if (layer.mask) {
				prepared.mask = PrepareMask(a_recipe, *layer.mask, a_inputs, size, stack->diagnostics_, where);
			}
			stack->layers_.push_back(std::move(prepared));
		}
		if (!stack->layers_.empty()) {
			stack->target_ = lab->Acquire(size);
			if (!stack->target_ || !lab->Scratch(size)) {
				stack->diagnostics_.push_back({ Severity::kError, "stack", "no render targets available" });
				stack->layers_.clear();
				stack->target_.reset();
			}
		}
		return stack;
	}

	namespace
	{
		// The largest-size entry of a cache keyed "<definition>@<size>"
		// (Mesh.h; masks and ripples use their name as the definition); null
		// when the definition has none. The largest is the one a stack read.
		template <class T>
		std::shared_ptr<T> LargestOf(const std::unordered_map<std::string, std::shared_ptr<T>>& a_cache, std::string_view a_definition)
		{
			std::shared_ptr<T> best;
			std::uint32_t      bestSize = 0;
			for (const auto& [key, value] : a_cache) {
				if (KeyDefinition(key) != a_definition) {
					continue;
				}
				const auto size = KeySize(key).value_or(0);
				if (!best || size > bestSize) {
					best = value;
					bestSize = size;
				}
			}
			return best;
		}

		// A rendered mask of that name, when the geometry has one.
		std::shared_ptr<RenderedMask> CachedMask(const GeometryInputs& a_inputs, std::string_view a_name)
		{
			return a_inputs.masks ? LargestOf(*a_inputs.masks, a_name) : nullptr;
		}

		// A bake of that definition from the mesh entry, when it has one.
		std::shared_ptr<TextureLab::Target> CachedBake(const MeshEntry* a_entry, std::string_view a_definition)
		{
			return a_entry ? LargestOf(a_entry->bakes, a_definition) : nullptr;
		}

		// The mask that is exactly one material channel, which a layer reads
		// from the map itself and nothing renders.
		std::optional<MaterialChannel> SingleChannelOf(const Recipe& a_recipe, const Mask& a_mask)
		{
			const auto  program = Program::Parse(a_mask.text);
			const auto* source = program && program->OpCount() == 1 && program->References().size() == 1 ? a_recipe.FindSource(program->References()[0]) : nullptr;
			const auto* channel = source ? Get<MaterialSource>(source->kind) : nullptr;
			return channel ? std::optional{ channel->channel } : std::nullopt;
		}
	}

	std::optional<PreparedSource> Compositor::InspectSource(const Recipe& a_recipe, std::string_view a_name, const GeometryInputs& a_inputs) const
	{
		if (a_recipe.FindMask(a_name)) {
			PreparedSource prepared;
			if (auto rendered = CachedMask(a_inputs, a_name)) {
				prepared.texture = RE::NiPointer<RE::NiSourceTexture>{ rendered->Texture() };
				prepared.sampling.channel = rendered->Vector() ? ShaderChannel::kRgb : ShaderChannel::kR;
				prepared.sampling.meshSpace = true;
				prepared.animated = rendered->Animated();
				prepared.problem = rendered->Problem();
				prepared.rendered = std::move(rendered);
			} else {
				prepared.problem = kNotRendered;
			}
			return prepared;
		}
		const auto* source = a_recipe.FindSource(a_name);
		if (!source) {
			return std::nullopt;
		}
		const auto     entry = CachedMesh(a_inputs.geometry.get());
		PreparedSource prepared;
		prepared.animated = IsAnimated(a_recipe, *source);
		// A bake that could not be made because the mesh could not be read
		// says so; anything else absent was never read by a stack.
		const auto notRendered = [&] {
			prepared.problem = entry && !entry->mesh && !entry->problem.empty() ? "the mesh could not be read: " + entry->problem : std::string{ kNotRendered };
		};
		const auto baked = [&](const std::string& a_definition, ShaderChannel a_channel) {
			prepared.sampling.meshSpace = true;
			if (const auto target = CachedBake(entry.get(), a_definition)) {
				prepared.texture = RE::NiPointer<RE::NiSourceTexture>{ target->Texture() };
				prepared.sampling.channel = a_channel;
			} else {
				notRendered();
			}
		};
		Match(
			source->kind,
			[&](const ImageSource& image) {
				const auto loaded = images_.find(Lower(image.path));
				if (loaded == images_.end()) {
					prepared.problem = kNotRendered;
					return;
				}
				prepared.texture = loaded->second;
				if (!prepared.texture) {
					prepared.problem = std::format("image '{}' did not load", image.path);
				}
				prepared.sampling.channel = ShaderChannelOf(image.channel);
				prepared.sampling.meshSpace = image.space == ImageSpace::kMesh;
				prepared.sampling.transform.mirrorU = image.mirror[0];
				prepared.sampling.transform.mirrorV = image.mirror[1];
				prepared.sampling.transform.transpose = image.transpose;
				prepared.sampling.transform.sourceMip = image.mip;
				prepared.scroll = image.scroll;
				prepared.tile = image.tile;
			},
			[&](const MaterialSource& material) {
				// The material's own maps, resolved from the record taken at apply;
				// a derived map only when a prepare already rendered it.
				auto pick = PickMaterialChannel(material.channel, a_inputs, false);
				prepared.texture = pick.texture;
				prepared.sampling.channel = pick.channel;
				prepared.sampling.meshSpace = true;
				prepared.problem = pick.problem;
				if (prepared.problem.empty() && !RealTexture(prepared.texture)) {
					prepared.problem = DescribeTexture(prepared.texture);
				}
			},
			[&](const BakeSource& bake) {
				baked(DefinitionOf(bake.bake), Is<PositionBake>(bake.bake) || Is<LocalPositionBake>(bake.bake) ? ShaderChannel::kRgb : ShaderChannel::kR);
			},
			[&](const DistanceSource& distance) {
				baked(DefinitionOf(distance), ShaderChannel::kR);
			},
			[&](const UvSource& uv) {
				baked(DefinitionOf(uv.axis), ShaderChannel::kR);
			},
			[&](const RippleSource&) {
				prepared.sampling.meshSpace = true;
				const auto rendered = a_inputs.ripples ? LargestOf(*a_inputs.ripples, a_name) : nullptr;
				if (!rendered) {
					notRendered();
					return;
				}
				prepared.texture = RE::NiPointer<RE::NiSourceTexture>{ rendered->Texture() };
				prepared.animated = true;
				prepared.ripple = rendered;
			},
			[&](const MaterialClustersSource& clusters) {
				// The map as the last prepare left it: rendered under these
				// settings, failed, or never asked for.
				prepared.sampling.meshSpace = true;
				const DerivedMaps& derived = *a_inputs.derived;
				if (derived.clusters && derived.clusterSettings == SettingsOf(clusters)) {
					prepared.texture = RE::NiPointer<RE::NiSourceTexture>{ derived.clusters->Texture() };
					return;
				}
				prepared.problem = derived.clustersTried && !derived.clustersProblem.empty() ? derived.clustersProblem : std::string{ kNotRendered };
			});
		return prepared;
	}

	std::optional<PreparedMask> Compositor::InspectMask(const Recipe& a_recipe, std::string_view a_name, const GeometryInputs& a_inputs) const
	{
		const auto* mask = a_recipe.FindMask(a_name);
		if (!mask) {
			return std::nullopt;
		}
		PreparedMask prepared;
		prepared.animated = IsAnimated(a_recipe, *mask);
		if (auto rendered = CachedMask(a_inputs, a_name)) {
			prepared.texture = RE::NiPointer<RE::NiSourceTexture>{ rendered->Texture() };
			prepared.animated = rendered->Animated();
			prepared.problem = rendered->Problem();
			prepared.rendered = std::move(rendered);
			return prepared;
		}
		if (const auto channel = SingleChannelOf(a_recipe, *mask)) {
			auto pick = PickMaterialChannel(*channel, a_inputs, false);
			if (!pick.problem.empty() || !RealTexture(pick.texture)) {
				prepared.problem = pick.problem.empty() ? DescribeTexture(pick.texture) + "; evaluates as white" : pick.problem;
				return prepared;
			}
			prepared.texture = pick.texture;
			prepared.channel = pick.channel;
			return prepared;
		}
		prepared.problem = kNotRendered;
		return prepared;
	}

	// ---------------------------------------------------------- rendered masks

	RE::NiSourceTexture* RenderedMask::Texture() const noexcept
	{
		return target_ ? target_->Texture() : nullptr;
	}

	std::shared_ptr<RenderedMask> Compositor::PrepareRenderedMask(const Recipe& a_recipe, std::string_view a_name, const GeometryInputs& a_inputs, TextureSize a_size, std::uint32_t a_depth)
	{
		constexpr std::uint32_t kMaxMaskDepth = 8;
		const auto* mask = a_recipe.FindMask(a_name);
		if (!mask || !a_inputs.masks) {
			return nullptr;
		}
		const auto key = std::format("{}@{}", a_name, a_size.Pixels());
		if (const auto it = a_inputs.masks->find(key); it != a_inputs.masks->end()) {
			return it->second;
		}
		auto rendered = std::make_shared<RenderedMask>();
		(*a_inputs.masks)[key] = rendered;  // in the cache before recursion, so a cycle finds an unfinished mask
		auto& r = *rendered;
		const auto fail = [&](std::string a_problem) {
			r.problem_ = std::move(a_problem);
			r.program_.reset();
			r.target_.reset();
			return rendered;
		};
		if (a_depth > kMaxMaskDepth) {
			return fail(std::format("masks nest deeper than {}", kMaxMaskDepth));
		}
		auto program = Program::Parse(mask->text);
		if (!program) {
			return fail(program.error());
		}
		const auto graph = GraphFor(a_recipe);

		// Every name the program reads: an image slot or a per-tick value.
		std::vector<Diagnostic> ignored;
		for (const auto& name : program->References()) {
			RenderedMask::RefBinding binding;
			if (a_recipe.FindSource(name) || a_recipe.FindMask(name)) {
				if (r.textures_.size() >= TextureLab::kProgramTextures) {
					return fail(std::format("reads more than {} images", TextureLab::kProgramTextures));
				}
				auto source = PrepareSource(a_recipe, Ref{ name }, a_inputs, a_size, ignored, "mask", a_depth + 1);
				if (!source || !source->problem.empty()) {
					return fail(std::format("'@{}': {}", name, source ? source->problem : "not found"));
				}
				if (source->rendered) {
					if (!source->rendered->program_ && source->rendered->problem_.empty()) {
						return fail(std::format("'@{}' reads back into this mask", name));
					}
					r.dependencies_.push_back(source->rendered);
				}
				binding.isTexture = true;
				binding.texture = static_cast<std::uint32_t>(r.textures_.size());
				r.animated_ = r.animated_ || source->animated;
				r.textures_.push_back(std::move(*source));
			} else if (graph && graph->Index(name)) {
				binding.signal = name;
				r.animated_ = true;
			} else {
				return fail(std::format("'@{}' is not a source, mask or signal", name));
			}
			r.refs_.push_back(std::move(binding));
			if (r.refs_.size() > TextureLab::kProgramRefs) {
				return fail(std::format("reads more than {} names", TextureLab::kProgramRefs));
			}
		}
		for (const auto& curveName : program->Curves()) {
			const auto* curve = a_recipe.FindCurve(curveName);
			if (!curve) {
				return fail(std::format("curve '@{}' not found", curveName));
			}
			const auto curveProgram = ParseCurve(curve->text);
			if (!curveProgram) {
				return fail(std::format("curve '@{}': {}", curveName, curveProgram.error()));
			}
			std::array<float, 256> values{};
			for (std::size_t i = 0; i < values.size(); ++i) {
				values[i] = ApplyCurve(*curveProgram, static_cast<float>(i) / 255.0f, 0.5f);
			}
			auto lookup = TextureLab::GetSingleton()->CreateLookup(values);
			if (!lookup) {
				return fail("curve lookup could not be created");
			}
			r.curves_.push_back(std::move(lookup));
			if (r.curves_.size() > TextureLab::kProgramCurves) {
				return fail(std::format("calls more than {} curves", TextureLab::kProgramCurves));
			}
		}
		const auto type = program->Check([&](std::string_view name) -> std::optional<ValueType> {
			if (const auto* source = a_recipe.FindSource(name)) {
				return SourceType(*source);
			}
			if (a_recipe.FindMask(name)) {
				const auto dep = CachedMask(a_inputs, name);
				return dep && dep->Vector() ? ValueType::kVec3 : ValueType::kScalar;
			}
			return graph ? graph->TypeOf(name) : std::nullopt;
		});
		if (!type) {
			return fail(type.error());
		}
		r.vector_ = *type != ValueType::kScalar;
		r.animated_ = r.animated_ || program->UsesTime();
		if (!TextureLab::GetSingleton()->InterpreterAvailable()) {
			return fail("the interpreter shader did not compile (see the log at start)");
		}
		r.target_ = TextureLab::GetSingleton()->Acquire(a_size);
		if (!r.target_) {
			return fail("no render target available");
		}
		r.program_ = std::move(*program);
		return rendered;
	}

	// ------------------------------------------------------------------ bakes

	std::expected<std::shared_ptr<MeshEntry>, std::string> Compositor::MeshOf(RE::BSGeometry* a_geometry)
	{
		return meshes_.Get(a_geometry, nowMS_, GetSettings().verboseLogging);
	}

	std::shared_ptr<const MeshEntry> Compositor::CachedMesh(RE::BSGeometry* a_geometry) const noexcept
	{
		return meshes_.Cached(a_geometry);
	}

	void Compositor::SweepMeshes(std::uint32_t a_nowMS, std::span<RE::BSGeometry* const> a_bound)
	{
		lastSweepMS_ = a_nowMS;
		meshes_.Sweep(a_nowMS, kMeshMaxAgeMS, a_bound, GetSettings().verboseLogging);
	}

	const Compositor::MaterialRecord& Compositor::AnalyseMaterial(const MaterialInputs& a_material)
	{
		const auto key = std::make_pair(a_material.rmaos.get(), a_material.diffuse.get());
		auto&      record = materials_[key];
		if (record.sample || !record.problem.empty()) {
			return record;
		}
		record.rmaos = a_material.rmaos;
		record.diffuse = a_material.diffuse;
		if (!RealTexture(a_material.rmaos) || !RealTexture(a_material.diffuse)) {
			record.problem = std::format("RMAOS {}; diffuse {}", DescribeTexture(a_material.rmaos), DescribeTexture(a_material.diffuse));
			return record;
		}
		// The readback waits on the GPU; the line before it names the step
		// should the wait never end.
		const bool verbose = GetSettings().verboseLogging;
		if (verbose) {
			logger::info("material '{}': sampling", a_material.rmaos->name.c_str() ? a_material.rmaos->name.c_str() : "?");
		}
		auto sample = TextureLab::GetSingleton()->SampleMaterial(a_material.rmaos.get(), a_material.diffuse.get());
		if (!sample) {
			record.problem = "the maps could not be read back";
			return record;
		}
		record.sample = std::make_shared<const MaterialSample>(std::move(*sample));
		record.analysis = std::make_shared<const MaterialAnalysis>(ClusterMaterial(*record.sample, ClusterSettings{}));
		if (verbose) {
			logger::info("material '{}': sampled {}x{}, {} clusters", a_material.rmaos->name.c_str() ? a_material.rmaos->name.c_str() : "?", record.sample->width, record.sample->height, record.analysis->clusters.size());
		}
		return record;
	}

	const Compositor::MaterialRecord* Compositor::CachedMaterial(const MaterialInputs& a_material) const noexcept
	{
		const auto it = materials_.find(std::make_pair(a_material.rmaos.get(), a_material.diffuse.get()));
		return it == materials_.end() ? nullptr : &it->second;
	}

	void Compositor::ClearMaterials() noexcept
	{
		materials_.clear();
	}

	void Compositor::ClearMeshes() noexcept
	{
		meshes_.Clear();
	}

	std::expected<std::shared_ptr<TextureLab::Target>, std::string> Compositor::BakeInto(MeshEntry& a_entry, const std::string& a_key, TextureSize a_size, const std::function<BakeBuffers()>& a_buffers)
	{
		if (const auto it = a_entry.bakes.find(a_key); it != a_entry.bakes.end()) {
			return it->second;
		}
		auto* lab = TextureLab::GetSingleton();
		if (!lab->Init() || !lab->BakingAvailable()) {
			return std::unexpected("the bake pass is unavailable (see the log at start)");
		}
		const auto buffers = a_buffers();
		if (!buffers.problem.empty()) {
			return std::unexpected(buffers.problem);
		}
		auto target = lab->Acquire(a_size);
		if (!target) {
			return std::unexpected("no render target available");
		}
		if (!lab->BakeMesh(*target, buffers)) {
			return std::unexpected("the bake pass failed");
		}
		if (GetSettings().verboseLogging) {
			logger::info("bake '{}' on '{}' at {} px", KeyDefinition(a_key), a_entry.geometry && a_entry.geometry->name.c_str() ? a_entry.geometry->name.c_str() : "?", a_size.Pixels());
		}
		a_entry.bakes[a_key] = target;
		return target;
	}

	std::expected<std::shared_ptr<TextureLab::Target>, std::string> Compositor::PrepareBake(const BakeSource& a_bake, const GeometryInputs& a_inputs, TextureSize a_size)
	{
		const auto entry = MeshOf(a_inputs.geometry.get());
		if (!entry) {
			return std::unexpected(entry.error());
		}
		// The id maps come from the analysis stored with the read; every
		// other kind from the mesh alone.
		return BakeInto(**entry, BakeKeyOf(a_bake.bake, a_size), a_size, [&] {
			if (Is<ComponentIdBake>(a_bake.bake)) {
				return BuildIslandBake(*(*entry)->mesh, (*entry)->analysis, IslandSource::kComponent);
			}
			if (Is<ChartIdBake>(a_bake.bake)) {
				return BuildIslandBake(*(*entry)->mesh, (*entry)->analysis, IslandSource::kChart);
			}
			return BuildBake(*(*entry)->mesh, a_bake.bake);
		});
	}

	std::expected<std::shared_ptr<TextureLab::Target>, std::string> Compositor::PrepareDistance(const DistanceSource& a_distance, const GeometryInputs& a_inputs, TextureSize a_size)
	{
		const auto entry = MeshOf(a_inputs.geometry.get());
		if (!entry) {
			return std::unexpected(entry.error());
		}
		// The key names the node, not its position: a node resolves to one
		// bind-pose point per geometry, and the snapshot can then find the
		// bake without looking the node up.
		std::optional<Vec3> from = Match(
			a_distance.from,
			[&](const Vec3& point) { return std::optional{ point }; },
			[&](const std::string& node) { return NodeBindPosition(a_inputs.geometry.get(), a_inputs.root.get(), node); });
		if (!from) {
			return std::unexpected(std::format("node '{}' was not found on the wearer", Get<std::string>(a_distance.from) ? *Get<std::string>(a_distance.from) : ""));
		}
		return BakeInto(**entry, DistanceKeyOf(a_distance, a_size), a_size, [&] { return BuildDistanceBake(*(*entry)->mesh, *from); });
	}

	// ---------------------------------------------------------------- ripples

	RE::NiSourceTexture* RenderedRipple::Texture() const noexcept
	{
		return target_ ? target_->Texture() : nullptr;
	}

	std::expected<std::shared_ptr<RenderedRipple>, std::string> Compositor::PrepareRipple(const Source& a_source, const RippleSource& a_ripple, const GeometryInputs& a_inputs, TextureSize a_size)
	{
		if (!a_inputs.ripples) {
			return std::unexpected("no geometry to ripple over");
		}
		const auto key = std::format("{}@{}", a_source.name, a_size.Pixels());
		if (const auto it = a_inputs.ripples->find(key); it != a_inputs.ripples->end()) {
			return it->second;
		}
		auto* lab = TextureLab::GetSingleton();
		if (!lab->Init() || !lab->RippleAvailable()) {
			return std::unexpected("the ripple pass is unavailable (see the log at start)");
		}
		const auto entry = MeshOf(a_inputs.geometry.get());
		if (!entry) {
			return std::unexpected(entry.error());
		}
		// The position bake every ripple on this geometry shares with any
		// position source, under the same definition key.
		auto positions = BakeInto(**entry, BakeKeyOf(PositionBake{}, a_size), a_size, [&] { return BuildBake(*(*entry)->mesh, PositionBake{}); });
		if (!positions) {
			return std::unexpected(positions.error());
		}
		auto ripple = std::make_shared<RenderedRipple>();
		ripple->target_ = lab->Acquire(a_size);
		if (!ripple->target_) {
			return std::unexpected("no render target available");
		}
		ripple->positions_ = *positions;
		ripple->source_ = a_ripple;
		ripple->fallbackOrigin_ = (*entry)->mesh->center;
		ripple->geometry_ = a_inputs.geometry;
		ripple->root_ = a_inputs.root;
		// A pooled target keeps its last content; a pass with no firings paints it black.
		TextureLab::RipplePass clear;
		clear.positions = ripple->positions_->Texture();
		lab->RenderRipple(*ripple->target_, clear);
		(*a_inputs.ripples)[key] = ripple;
		return ripple;
	}

	void Compositor::RenderRipple(RenderedRipple& a_ripple, const SignalState& a_signals, float a_time)
	{
		if (!a_ripple.target_ || !a_ripple.positions_ || a_ripple.renderedTick_ == tick_) {
			return;
		}
		a_ripple.renderedTick_ = tick_;
		TextureLab::RipplePass pass;
		pass.positions = a_ripple.positions_->Texture();
		pass.frame = kPositionFrame;
		pass.speed = a_signals.Resolve(a_ripple.source_.speed);
		pass.width = a_signals.Resolve(a_ripple.source_.width);
		pass.decay = a_signals.Resolve(a_ripple.source_.decay);
		pass.disc = a_ripple.source_.shape == RippleShape::kDisc;
		for (const auto& firing : a_signals.Firings(a_ripple.source_.trigger.name)) {
			if (pass.firingCount >= pass.firings.size()) {
				break;
			}
			// Where the front starts: the firing's position, else its node, else the piece's centre.
			std::optional<Vec3> origin;
			if (firing.payload.position) {
				origin = ToRootSpace(a_ripple.root_.get(), *firing.payload.position);
			} else if (!firing.payload.node.empty()) {
				origin = NodeBindPosition(a_ripple.geometry_.get(), a_ripple.root_.get(), firing.payload.node);
			}
			pass.firings[pass.firingCount++] = { origin.value_or(a_ripple.fallbackOrigin_), std::max(0.0f, a_time - firing.startTime) };
		}
		if (pass.firingCount == 0 && !a_ripple.hadFirings_) {
			return;  // still black; nothing to draw or clear
		}
		a_ripple.hadFirings_ = pass.firingCount > 0;
		TextureLab::GetSingleton()->RenderRipple(*a_ripple.target_, pass);
	}

	namespace
	{
		// The sampling of a source with this tick's scroll and tile.
		TextureLab::LayerInput SamplingNow(const PreparedSource& a_source, const SignalState& a_signals)
		{
			auto input = a_source.sampling;
			if (a_source.scroll) {
				const auto scroll = a_signals.Resolve(*a_source.scroll);
				input.transform.uOffset = scroll.x;
				input.transform.vOffset = scroll.y;
			}
			if (a_source.tile) {
				const auto tile = a_signals.Resolve(*a_source.tile);
				input.transform.tileU = std::max(tile.x, 0.01f);
				input.transform.tileV = std::max(tile.y, 0.01f);
			}
			return input;
		}
	}

	void Compositor::RenderMask(RenderedMask& a_mask, const SignalState& a_signals, float a_time)
	{
		if (!a_mask.program_ || !a_mask.target_ || !a_mask.problem_.empty()) {
			return;
		}
		if (a_mask.renderedOnce_ && (!a_mask.animated_ || a_mask.renderedTick_ == tick_)) {
			return;
		}
		for (const auto& dependency : a_mask.dependencies_) {
			if (dependency) {
				RenderMask(*dependency, a_signals, a_time);  // depth bounded at preparation
			}
		}
		for (const auto& texture : a_mask.textures_) {
			if (texture.ripple) {
				RenderRipple(*texture.ripple, a_signals, a_time);
			}
		}
		// PrepareRenderedMask refused this mask if it read more than the pass
		// holds, so the counts fit; the checks keep a write inside the arrays
		// regardless.
		TextureLab::ProgramPass pass;
		pass.code = a_mask.program_->Code();
		for (const auto& binding : a_mask.refs_) {
			if (pass.refCount >= pass.refs.size()) {
				return;
			}
			TextureLab::ProgramRef ref;
			ref.isTexture = binding.isTexture;
			ref.texture = binding.texture;
			if (!binding.isTexture) {
				ref.value = AsVec3(a_signals.ValueOf(binding.signal));
			}
			pass.refs[pass.refCount++] = ref;
		}
		for (const auto& source : a_mask.textures_) {
			if (pass.textureCount >= pass.textures.size()) {
				return;
			}
			pass.textures[pass.textureCount++] = { source.texture.get(), SamplingNow(source, a_signals), source.normalize };
		}
		for (const auto& curve : a_mask.curves_) {
			if (pass.curveCount >= pass.curves.size()) {
				return;
			}
			pass.curves[pass.curveCount++] = curve.get();
		}
		pass.time = a_time;
		pass.vectorResult = a_mask.vector_;
		if (TextureLab::GetSingleton()->RenderProgram(*a_mask.target_, pass)) {
			a_mask.renderedOnce_ = true;
			a_mask.renderedTick_ = tick_;
		}
	}

	void Compositor::Render(RenderedStack& a_stack, const SignalState& a_signals, float a_time, const LayerFilter& a_filter)
	{
		if (a_stack.layers_.empty()) {
			return;
		}
		const bool filterChanged = a_stack.filter_ != a_filter;
		if (!a_stack.animated_ && a_stack.renderedOnce_ && !filterChanged) {
			return;
		}
		std::size_t shown = 0;
		for (const auto& prepared : a_stack.layers_) {
			if (a_filter.Hides(prepared.index)) {
				continue;
			}
			++shown;
			if (prepared.source && prepared.source->ripple) {
				RenderRipple(*prepared.source->ripple, a_signals, a_time);
			}
			if (prepared.source && prepared.source->rendered) {
				RenderMask(*prepared.source->rendered, a_signals, a_time);
			}
			if (prepared.mask && prepared.mask->rendered) {
				RenderMask(*prepared.mask->rendered, a_signals, a_time);
			}
		}
		if (shown == 0) {
			a_stack.latest_ = nullptr;
			a_stack.renderedOnce_ = true;
			a_stack.filter_ = a_filter;
			return;
		}
		auto*               lab = TextureLab::GetSingleton();
		TextureLab::Target* own = a_stack.target_.get();
		TextureLab::Target* scratch = lab->Scratch(a_stack.size_);
		if (!own || !scratch) {
			return;
		}
		// Writes alternate between the stack's target and the scratch; the
		// last layer must land in the stack's own target.
		TextureLab::Target* previous = nullptr;
		TextureLab::Target* write = shown % 2 == 1 ? own : scratch;
		TextureLab::Target* other = write == own ? scratch : own;
		for (const auto& prepared : a_stack.layers_) {
			if (a_filter.Hides(prepared.index)) {
				continue;
			}
			const auto&            layer = *prepared.layer;
			TextureLab::LayerParams params;
			params.mode = TextureLab::Mode::kLayer;
			auto& pass = params.layer;
			pass.previous = previous ? previous->Texture() : a_stack.base_.get();
			if (prepared.source) {
				pass.source = prepared.source->texture.get();
				pass.input = SamplingNow(*prepared.source, a_signals);
				pass.normalize = prepared.source->normalize;
			} else if (const auto* constant = Get<Vec3>(layer.source)) {
				pass.color[0] = constant->x;
				pass.color[1] = constant->y;
				pass.color[2] = constant->z;
			}
			if (layer.color) {
				const auto colour = a_signals.Resolve(*layer.color);
				pass.color[0] *= colour.x;
				pass.color[1] *= colour.y;
				pass.color[2] *= colour.z;
			}
			pass.opacity = a_signals.Resolve(layer.opacity);
			pass.blend = BlendShaderMode(layer.blend);
			pass.channels = ChannelBits(layer.channels);
			if (prepared.mask && prepared.mask->texture) {
				pass.mask = prepared.mask->texture.get();
				pass.maskChannel = prepared.mask->channel;
			}
			pass.curve = prepared.curve.get();
			if (!lab->Render(*write, nullptr, params)) {
				return;
			}
			previous = write;
			std::swap(write, other);
		}
		a_stack.latest_ = previous;
		a_stack.renderedOnce_ = true;
		a_stack.filter_ = a_filter;
	}
}
