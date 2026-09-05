#include "Compositor.h"

#include "Expression.h"
#include "MeshReader.h"
#include "RecipeStore.h"

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

		std::uint32_t ChannelIndex(ImageChannel a_channel)
		{
			switch (a_channel) {
			case ImageChannel::kR:
				return 0;
			case ImageChannel::kG:
				return 1;
			case ImageChannel::kB:
				return 2;
			case ImageChannel::kA:
				return 3;
			case ImageChannel::kLuma:
				return 5;
			default:
				return 4;
			}
		}

		std::uint32_t BlendIndex(Blend a_blend)
		{
			switch (a_blend) {
			case Blend::kMultiply:
				return 1;
			case Blend::kAdd:
				return 2;
			case Blend::kSubtract:
				return 3;
			case Blend::kScreen:
				return 4;
			case Blend::kLerp:
				return 5;
			default:
				return 0;
			}
		}

		std::uint32_t ChannelBits(const ChannelSet& a_set)
		{
			return (a_set.r ? 1u : 0u) | (a_set.g ? 2u : 0u) | (a_set.b ? 4u : 0u) | (a_set.a ? 8u : 0u);
		}

		// Which material texture and channel a material source reads.
		struct MaterialChannelPick
		{
			RE::NiPointer<RE::NiSourceTexture> texture;
			std::uint32_t                      channel = 4;
			std::string                        problem;
		};

		// The map a slot edits in place, when the slot has one.
		RE::NiPointer<RE::NiSourceTexture> BaseMapFor(Slot a_slot, const MaterialInputs& a_material)
		{
			switch (a_slot) {
			case Slot::kDiffuse:
				return a_material.diffuse;
			case Slot::kNormal:
				return a_material.normal;
			case Slot::kRmaos:
				return a_material.rmaos;
			case Slot::kHeight:
				return a_material.displacement;
			default:
				return nullptr;
			}
		}

		// Many PBR sets ship a displacement map that is a real texture and
		// entirely black (NOTES 46): flat when its mean sits at either end.
		bool FlatDisplacement(const MaterialInputs& a_material)
		{
			if (!RealTexture(a_material.displacement)) {
				return true;
			}
			const float mean = TextureLab::GetSingleton()->MeanChannel(a_material.displacement.get(), 0);
			return !(mean > 0.02f && mean < 0.98f);
		}

		MaterialChannelPick PickMaterialChannel(MaterialChannel a_channel, const MaterialInputs& a_material)
		{
			switch (a_channel) {
			case MaterialChannel::kDiffuseRgb:
				return { a_material.diffuse, 4, {} };
			case MaterialChannel::kDiffuseLuma:
				return { a_material.diffuse, 5, {} };
			case MaterialChannel::kRoughness:
				return { a_material.rmaos, 0, {} };
			case MaterialChannel::kMetallic:
				return { a_material.rmaos, 1, {} };
			case MaterialChannel::kOcclusion:
				return { a_material.rmaos, 2, {} };
			case MaterialChannel::kReflectance:
				return { a_material.rmaos, 3, {} };
			case MaterialChannel::kDisplacement:
				return { a_material.displacement, 0, {} };
			case MaterialChannel::kRelief:
				// A flat height map carries no relief; the occlusion channel does.
				if (RealTexture(a_material.displacement) && !FlatDisplacement(a_material)) {
					return { a_material.displacement, 0, {} };
				}
				return { a_material.rmaos, 2, {} };
			case MaterialChannel::kNormalSlope:
				return { nullptr, 0, "normalSlope needs the interpreter pass (phase 3)" };
			}
			return { nullptr, 0, "unknown channel" };
		}
	}

	MaterialInputs MaterialInputs::From(const PBRMaterialLayout& a_material)
	{
		MaterialInputs in;
		in.diffuse = a_material.diffuseTexture;
		in.normal = a_material.normalTexture;
		in.rmaos = a_material.rmaosTexture;
		in.displacement = a_material.displacementTexture;
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

	std::optional<PreparedSource> Compositor::PrepareSource(const Recipe& a_recipe, const Ref& a_ref, const GeometryInputs& a_inputs, std::uint32_t a_size, std::vector<Diagnostic>& a_out, const std::string& a_where)
	{
		const auto& a_material = a_inputs.material;
		if (a_recipe.FindMask(a_ref.name)) {
			// A mask read as a source: its rendered target, by mesh UV.
			auto rendered = PrepareRenderedMask(a_recipe, a_ref.name, a_inputs, a_size, 0);
			PreparedSource prepared;
			if (!rendered || !rendered->Problem().empty()) {
				prepared.problem = rendered ? rendered->Problem() : "mask could not be prepared";
				a_out.push_back({ Severity::kWarning, a_where, std::format("'@{}': {}; the layer is skipped", a_ref.name, prepared.problem) });
				return prepared;
			}
			prepared.texture = RE::NiPointer<RE::NiSourceTexture>{ rendered->Texture() };
			prepared.sampling.channel = rendered->Vector() ? 4u : 0u;
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
				prepared.sampling.channel = ChannelIndex(image.channel);
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
				auto pick = PickMaterialChannel(material.channel, a_material);
				prepared.texture = pick.texture;
				prepared.sampling.channel = pick.channel;
				prepared.sampling.meshSpace = true;
				prepared.problem = pick.problem;
				if (prepared.problem.empty() && !RealTexture(prepared.texture)) {
					prepared.problem = DescribeTexture(prepared.texture);
				}
			},
			[&](const BakeSource& bake) {
				auto target = PrepareBake(*source, bake, a_inputs, a_size);
				if (!target) {
					prepared.problem = target.error();
					return;
				}
				prepared.texture = RE::NiPointer<RE::NiSourceTexture>{ (*target)->Texture() };
				prepared.sampling.channel = Is<PositionBake>(bake.bake) || Is<LocalPositionBake>(bake.bake) ? 4u : 0u;
				prepared.sampling.meshSpace = true;
			},
			[&](const DistanceSource& distance) {
				auto target = PrepareDistance(*source, distance, a_inputs, a_size);
				if (!target) {
					prepared.problem = target.error();
					return;
				}
				prepared.texture = RE::NiPointer<RE::NiSourceTexture>{ (*target)->Texture() };
				prepared.sampling.channel = 0;
				prepared.sampling.meshSpace = true;
			},
			[&](const RippleSource& ripple) {
				auto rendered = PrepareRipple(*source, ripple, a_inputs, a_size);
				if (!rendered) {
					prepared.problem = rendered.error();
					return;
				}
				prepared.texture = RE::NiPointer<RE::NiSourceTexture>{ (*rendered)->Texture() };
				prepared.sampling.channel = 0;
				prepared.sampling.meshSpace = true;
				prepared.animated = true;
				prepared.ripple = std::move(*rendered);
			},
			[&](const UvSource& uv) {
				const auto mesh = MeshOf(a_inputs);
				if (!mesh) {
					prepared.problem = mesh.error();
					return;
				}
				auto target = BakeInto(a_inputs, std::format("{}@{}", source->name, a_size), a_size, [&] { return BuildUvBake(**mesh, uv.axis); });
				if (!target) {
					prepared.problem = target.error();
					return;
				}
				prepared.texture = RE::NiPointer<RE::NiSourceTexture>{ (*target)->Texture() };
				prepared.sampling.channel = 0;
				prepared.sampling.meshSpace = true;
			});
		if (!prepared.problem.empty()) {
			a_out.push_back({ Severity::kWarning, a_where, std::format("'@{}': {}; the layer is skipped", a_ref.name, prepared.problem) });
		}
		return prepared;
	}

	// A mask that is exactly one material channel reads the map directly;
	// any other expression renders through the interpreter pass.
	std::optional<PreparedMask> Compositor::PrepareMask(const Recipe& a_recipe, const Ref& a_ref, const GeometryInputs& a_inputs, std::uint32_t a_size, std::vector<Diagnostic>& a_out, const std::string& a_where)
	{
		const auto* mask = a_recipe.FindMask(a_ref.name);
		if (!mask) {
			a_out.push_back({ Severity::kError, a_where, std::format("mask '@{}' not found", a_ref.name) });
			return std::nullopt;
		}
		PreparedMask prepared;
		prepared.animated = IsAnimated(a_recipe, *mask);
		const auto program = Program::Parse(mask->text);
		const auto* source = program && program->OpCount() == 1 && program->References().size() == 1 ? a_recipe.FindSource(program->References()[0]) : nullptr;
		const auto* channel = source ? Get<MaterialSource>(source->kind) : nullptr;
		if (channel) {
			auto pick = PickMaterialChannel(channel->channel, a_inputs.material);
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
		prepared.channel = 0;
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
			mean = channel < 4 ? lab->MeanChannel(a_source->texture.get(), channel) : lab->MeanLuminance(a_source->texture.get());
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
		auto  target = lab->Acquire(64);
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

	std::unique_ptr<RenderedStack> Compositor::Prepare(const Recipe& a_recipe, const MaterialOutput& a_output, const GeometryInputs& a_inputs, std::uint32_t a_size, std::uint32_t a_maxSize)
	{
		const auto& a_material = a_inputs.material;
		auto* lab = TextureLab::GetSingleton();
		if (!lab->Init()) {
			return nullptr;
		}
		auto stack = std::make_unique<RenderedStack>();
		stack->size_ = std::clamp<std::uint32_t>(a_size, 64, 4096);
		if (a_output.slot == Slot::kHeight && FlatDisplacement(a_material)) {
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
			stack->size_ = std::clamp<std::uint32_t>(largest, stack->size_, std::clamp<std::uint32_t>(a_maxSize, stack->size_, 4096));
		}
		stack->animated_ = IsAnimated(a_recipe, Output{ a_output });
		std::size_t index = 0;
		for (const auto& layer : a_output.stack) {
			const auto    where = std::format("layer {}", index);
			PreparedLayer prepared;
			prepared.layer = &layer;
			prepared.index = index++;
			if (const auto* ref = Get<Ref>(layer.source)) {
				prepared.source = PrepareSource(a_recipe, *ref, a_inputs, stack->size_, stack->diagnostics_, where);
				if (!prepared.source || !prepared.source->problem.empty()) {
					continue;
				}
			}
			if (layer.curve) {
				prepared.curve = BakeCurve(a_recipe, *layer.curve, prepared.source, stack->diagnostics_, where);
			}
			if (layer.mask) {
				prepared.mask = PrepareMask(a_recipe, *layer.mask, a_inputs, stack->size_, stack->diagnostics_, where);
			}
			stack->layers_.push_back(std::move(prepared));
		}
		if (!stack->layers_.empty()) {
			stack->target_ = lab->Acquire(stack->size_);
			if (!stack->target_ || !lab->Scratch(stack->size_)) {
				stack->diagnostics_.push_back({ Severity::kError, "stack", "no render targets available" });
				stack->layers_.clear();
				stack->target_.reset();
			}
		}
		return stack;
	}

	namespace
	{
		std::shared_ptr<TextureLab::Target> CachedBake(const GeometryInputs& a_inputs, std::string_view a_name);

		// A rendered mask of that name at any size, when the geometry has one.
		std::shared_ptr<RenderedMask> CachedMask(const GeometryInputs& a_inputs, std::string_view a_name)
		{
			if (!a_inputs.masks) {
				return nullptr;
			}
			const auto prefix = std::string{ a_name } + "@";
			for (const auto& [key, mask] : *a_inputs.masks) {
				if (key.starts_with(prefix)) {
					return mask;
				}
			}
			return nullptr;
		}
	}

	std::optional<PreparedSource> Compositor::InspectSource(const Recipe& a_recipe, std::string_view a_name, const GeometryInputs& a_inputs)
	{
		std::vector<Diagnostic> ignored;
		if (a_recipe.FindMask(a_name)) {
			// Inspection never prepares a mask; it shows what an output rendered.
			PreparedSource prepared;
			if (auto rendered = CachedMask(a_inputs, a_name)) {
				prepared.texture = RE::NiPointer<RE::NiSourceTexture>{ rendered->Texture() };
				prepared.sampling.channel = rendered->Vector() ? 4u : 0u;
				prepared.sampling.meshSpace = true;
				prepared.animated = rendered->Animated();
				prepared.problem = rendered->Problem();
				prepared.rendered = std::move(rendered);
			} else {
				prepared.problem = "not read by any output on this geometry";
			}
			return prepared;
		}
		if (const auto* source = a_recipe.FindSource(a_name); source && (Is<BakeSource>(source->kind) || Is<DistanceSource>(source->kind) || Is<RippleSource>(source->kind) || Is<UvSource>(source->kind))) {
			// Inspection never bakes or renders; it shows what an output made.
			PreparedSource prepared;
			prepared.sampling.meshSpace = true;
			const auto notRead = [&] {
				prepared.problem = a_inputs.mesh && !a_inputs.mesh->problem.empty() ? "the mesh could not be read: " + a_inputs.mesh->problem : "not read by any output on this geometry";
			};
			if (const auto* ripple = Get<RippleSource>(source->kind)) {
				(void)ripple;
				const auto prefix = std::string{ a_name } + "@";
				for (const auto& [key, rendered] : *a_inputs.ripples) {
					if (key.starts_with(prefix)) {
						prepared.texture = RE::NiPointer<RE::NiSourceTexture>{ rendered->Texture() };
						prepared.animated = true;
						prepared.ripple = rendered;
					}
				}
				if (!prepared.texture) {
					notRead();
				}
				return prepared;
			}
			if (auto target = CachedBake(a_inputs, a_name)) {
				prepared.texture = RE::NiPointer<RE::NiSourceTexture>{ target->Texture() };
				const auto* bake = Get<BakeSource>(source->kind);
				prepared.sampling.channel = bake && (Is<PositionBake>(bake->bake) || Is<LocalPositionBake>(bake->bake)) ? 4u : 0u;
			} else {
				notRead();
			}
			return prepared;
		}
		return PrepareSource(a_recipe, Ref{ std::string{ a_name } }, a_inputs, 0, ignored, "inspect");
	}

	std::optional<PreparedMask> Compositor::InspectMask(const Recipe& a_recipe, std::string_view a_name, const GeometryInputs& a_inputs)
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
		// A one-channel mask reads the map directly and has nothing rendered.
		std::vector<Diagnostic> ignored;
		return PrepareMask(a_recipe, Ref{ std::string{ a_name } }, a_inputs, 0, ignored, "inspect");
	}

	// ---------------------------------------------------------- rendered masks

	RE::NiSourceTexture* RenderedMask::Texture() const noexcept
	{
		return target_ ? target_->Texture() : nullptr;
	}

	std::shared_ptr<RenderedMask> Compositor::PrepareRenderedMask(const Recipe& a_recipe, std::string_view a_name, const GeometryInputs& a_inputs, std::uint32_t a_size, std::uint32_t a_depth)
	{
		constexpr std::uint32_t kMaxMaskDepth = 8;
		const auto* mask = a_recipe.FindMask(a_name);
		if (!mask || !a_inputs.masks) {
			return nullptr;
		}
		const auto key = std::format("{}@{}", a_name, a_size);
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
				auto source = PrepareSource(a_recipe, Ref{ name }, a_inputs, a_size, ignored, "mask");
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
		r.target_ = TextureLab::GetSingleton()->Acquire(std::clamp<std::uint32_t>(a_size, 64, 4096));
		if (!r.target_) {
			return fail("no render target available");
		}
		r.program_ = std::move(*program);
		return rendered;
	}

	// ------------------------------------------------------------------ bakes

	std::expected<std::shared_ptr<const MeshData>, std::string> Compositor::MeshOf(const GeometryInputs& a_inputs)
	{
		if (!a_inputs.mesh) {
			return std::unexpected("no geometry to read");
		}
		auto& slot = *a_inputs.mesh;
		if (!slot.tried) {
			slot.tried = true;
			auto mesh = ReadMesh(a_inputs.geometry.get());
			if (mesh) {
				slot.data = *mesh;
				std::size_t vertices = 0, triangles = 0;
				for (const auto& p : slot.data->partitions) {
					vertices += p.vertices.size();
					triangles += p.triangles.size();
				}
				logger::info("mesh '{}': {} partition(s), {} vertices, {} triangles, {}", a_inputs.geometry ? a_inputs.geometry->name.c_str() : "?", slot.data->partitions.size(), vertices, triangles, slot.data->origin);
			} else {
				slot.problem = mesh.error();
			}
		}
		if (!slot.data) {
			return std::unexpected(std::format("the mesh could not be read: {}", slot.problem));
		}
		return slot.data;
	}

	std::expected<std::shared_ptr<TextureLab::Target>, std::string> Compositor::BakeInto(const GeometryInputs& a_inputs, const std::string& a_key, std::uint32_t a_size, const std::function<BakeBuffers()>& a_buffers)
	{
		if (!a_inputs.bakes) {
			return std::unexpected("no geometry to bake from");
		}
		if (const auto it = a_inputs.bakes->find(a_key); it != a_inputs.bakes->end()) {
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
		auto target = lab->Acquire(std::clamp<std::uint32_t>(a_size, 64, 4096));
		if (!target) {
			return std::unexpected("no render target available");
		}
		if (!lab->BakeMesh(*target, buffers)) {
			return std::unexpected("the bake pass failed");
		}
		(*a_inputs.bakes)[a_key] = target;
		return target;
	}

	std::expected<std::shared_ptr<TextureLab::Target>, std::string> Compositor::PrepareBake(const Source& a_source, const BakeSource& a_bake, const GeometryInputs& a_inputs, std::uint32_t a_size)
	{
		const auto mesh = MeshOf(a_inputs);
		if (!mesh) {
			return std::unexpected(mesh.error());
		}
		return BakeInto(a_inputs, std::format("{}@{}", a_source.name, a_size), a_size, [&] { return BuildBake(**mesh, a_bake.bake); });
	}

	std::expected<std::shared_ptr<TextureLab::Target>, std::string> Compositor::PrepareDistance(const Source& a_source, const DistanceSource& a_distance, const GeometryInputs& a_inputs, std::uint32_t a_size)
	{
		const auto mesh = MeshOf(a_inputs);
		if (!mesh) {
			return std::unexpected(mesh.error());
		}
		std::optional<Vec3> from = Match(
			a_distance.from,
			[&](const Vec3& point) { return std::optional{ point }; },
			[&](const std::string& node) { return NodeBindPosition(a_inputs.geometry.get(), a_inputs.root.get(), node); });
		if (!from) {
			return std::unexpected(std::format("node '{}' was not found on the wearer", Get<std::string>(a_distance.from) ? *Get<std::string>(a_distance.from) : ""));
		}
		return BakeInto(a_inputs, std::format("{}@{}", a_source.name, a_size), a_size, [&] { return BuildDistanceBake(**mesh, *from); });
	}

	// ---------------------------------------------------------------- ripples

	RE::NiSourceTexture* RenderedRipple::Texture() const noexcept
	{
		return target_ ? target_->Texture() : nullptr;
	}

	std::expected<std::shared_ptr<RenderedRipple>, std::string> Compositor::PrepareRipple(const Source& a_source, const RippleSource& a_ripple, const GeometryInputs& a_inputs, std::uint32_t a_size)
	{
		if (!a_inputs.ripples) {
			return std::unexpected("no geometry to ripple over");
		}
		const auto key = std::format("{}@{}", a_source.name, a_size);
		if (const auto it = a_inputs.ripples->find(key); it != a_inputs.ripples->end()) {
			return it->second;
		}
		auto* lab = TextureLab::GetSingleton();
		if (!lab->Init() || !lab->RippleAvailable()) {
			return std::unexpected("the ripple pass is unavailable (see the log at start)");
		}
		const auto mesh = MeshOf(a_inputs);
		if (!mesh) {
			return std::unexpected(mesh.error());
		}
		// The position bake every ripple on this geometry shares; its key cannot
		// collide with a source name because names never contain '@'.
		auto positions = BakeInto(a_inputs, std::format("@position@{}", a_size), a_size, [&] { return BuildBake(**mesh, PositionBake{}); });
		if (!positions) {
			return std::unexpected(positions.error());
		}
		auto ripple = std::make_shared<RenderedRipple>();
		ripple->target_ = lab->Acquire(std::clamp<std::uint32_t>(a_size, 64, 4096));
		if (!ripple->target_) {
			return std::unexpected("no render target available");
		}
		ripple->positions_ = *positions;
		ripple->source_ = a_ripple;
		ripple->fallbackOrigin_ = (*mesh)->center;
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
			if (pass.firings.size() >= TextureLab::kRippleFirings) {
				break;
			}
			// Where the front starts: the firing's position, else its node, else the piece's centre.
			std::optional<Vec3> origin;
			if (firing.payload.position) {
				origin = ToRootSpace(a_ripple.root_.get(), *firing.payload.position);
			} else if (!firing.payload.node.empty()) {
				origin = NodeBindPosition(a_ripple.geometry_.get(), a_ripple.root_.get(), firing.payload.node);
			}
			pass.firings.push_back({ origin.value_or(a_ripple.fallbackOrigin_), std::max(0.0f, a_time - firing.startTime) });
		}
		if (pass.firings.empty() && !a_ripple.hadFirings_) {
			return;  // still black; nothing to draw or clear
		}
		a_ripple.hadFirings_ = !pass.firings.empty();
		TextureLab::GetSingleton()->RenderRipple(*a_ripple.target_, pass);
	}

	namespace
	{
		// A bake of that name at any size, when the geometry has one.
		std::shared_ptr<TextureLab::Target> CachedBake(const GeometryInputs& a_inputs, std::string_view a_name)
		{
			if (!a_inputs.bakes) {
				return nullptr;
			}
			const auto prefix = std::string{ a_name } + "@";
			for (const auto& [key, target] : *a_inputs.bakes) {
				if (key.starts_with(prefix)) {
					return target;
				}
			}
			return nullptr;
		}

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
		TextureLab::ProgramPass pass;
		pass.code = a_mask.program_->Code();
		for (const auto& binding : a_mask.refs_) {
			TextureLab::ProgramRef ref;
			ref.isTexture = binding.isTexture;
			ref.texture = binding.texture;
			if (!binding.isTexture) {
				ref.value = AsVec3(a_signals.ValueOf(binding.signal));
			}
			pass.refs.push_back(ref);
		}
		for (const auto& source : a_mask.textures_) {
			pass.textures.push_back({ source.texture.get(), SamplingNow(source, a_signals), source.normalize });
		}
		for (const auto& curve : a_mask.curves_) {
			pass.curves.push_back(curve.get());
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
			pass.blend = BlendIndex(layer.blend);
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
