#pragma once

// Sources to textures. For one geometry and one material output, the
// compositor loads the sources the stack reads, renders the layers in order
// through the lab's layer pass, and hands back the texture the binding
// puts in the slot. Phase 2 renders image fields, constant colours,
// material channels and masks that are one material channel; arbitrary
// mask expressions wait for the interpreter pass (phase 3) and evaluate as
// white with a diagnostic on the row.

#include "Mesh.h"
#include "PBRMaterial.h"
#include "PCH.h"
#include "Recipe.h"
#include "RuntimeTextures.h"
#include "Signals.h"

#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace WornEnchantmentPBR
{
	// The material channels a stack can read, taken from the geometry's own
	// material at apply time (before any slot is swapped).
	struct MaterialInputs
	{
		RE::NiPointer<RE::NiSourceTexture> diffuse;
		RE::NiPointer<RE::NiSourceTexture> normal;
		RE::NiPointer<RE::NiSourceTexture> rmaos;
		RE::NiPointer<RE::NiSourceTexture> displacement;

		[[nodiscard]] static MaterialInputs From(const PBRMaterialLayout& a_material);
	};

	class RenderedMask;
	class RenderedRipple;

	// One source as the renderer sees it: a texture plus how to sample it.
	// A mask read as a source is its rendered target, sampled by mesh UV.
	struct PreparedSource
	{
		RE::NiPointer<RE::NiSourceTexture> texture;
		TextureLab::LayerInput             sampling;  // channel, space, mirror, transpose, mip
		std::optional<Vec2Param>           scroll;    // resolved per tick
		std::optional<Vec2Param>           tile;
		bool                               animated = false;
		float                              normalize = 1.0f;  // 0.5 / mean luminance for colour fields
		std::string                        problem;           // empty when usable
		std::shared_ptr<RenderedMask>      rendered;          // set when the source is a mask
		std::shared_ptr<RenderedRipple>    ripple;            // set when the source is a ripple
	};

	// A mask as a layer reads it: one material channel straight from the
	// map, or a rendered mask's target; null texture means white.
	struct PreparedMask
	{
		RE::NiPointer<RE::NiSourceTexture> texture;
		std::uint32_t                      channel = 0;
		bool                               animated = false;
		std::string                        problem;
		std::shared_ptr<RenderedMask>      rendered;
	};

	// Rendered masks per geometry, keyed "<mask>@<size>", shared by every
	// stack on the geometry that reads them.
	using MaskCache = std::unordered_map<std::string, std::shared_ptr<RenderedMask>>;

	// What the compositor reads on one geometry: its material's maps and the
	// masks already rendered for it.
	// A bake rasterised once for a geometry at a size, keyed "<source>@<size>".
	using BakeCache = std::unordered_map<std::string, std::shared_ptr<TextureLab::Target>>;

	// The geometry's mesh, read once; the problem when it could not be.
	struct MeshSlot
	{
		bool                            tried = false;
		std::shared_ptr<const MeshData> data;
		std::string                     problem;
	};

	using RippleCache = std::unordered_map<std::string, std::shared_ptr<RenderedRipple>>;

	struct GeometryInputs
	{
		MaterialInputs                    material;
		RE::NiPointer<RE::BSGeometry>     geometry;  // for the bakes; null when unknown
		RE::NiPointer<RE::NiAVObject>     root;      // the actor's 3D root: the frame of the bind pose, and where nodes are looked up
		std::shared_ptr<MaskCache>        masks = std::make_shared<MaskCache>();
		std::shared_ptr<BakeCache>        bakes = std::make_shared<BakeCache>();
		std::shared_ptr<RippleCache>      ripples = std::make_shared<RippleCache>();
		std::shared_ptr<MeshSlot>         mesh = std::make_shared<MeshSlot>();  // read on the first bake
	};

	// Fronts expanding from a trigger's live firings over the position bake,
	// rendered each tick into its own target.
	class RenderedRipple
	{
	public:
		[[nodiscard]] RE::NiSourceTexture* Texture() const noexcept;

	private:
		friend class Compositor;
		std::shared_ptr<TextureLab::Target> target_;
		std::shared_ptr<TextureLab::Target> positions_;  // the geometry's position bake
		RippleSource                        source_;
		Vec3                                fallbackOrigin_;  // the geometry's bound centre, for firings without a place
		RE::NiPointer<RE::BSGeometry>       geometry_;
		RE::NiPointer<RE::NiAVObject>       root_;
		bool                                hadFirings_ = false;
		std::uint64_t                       renderedTick_ = 0;
	};

	// A mask evaluated per texel by the interpreter pass into its own
	// target: once when static, each tick when it reads a signal, time or a
	// moving source. Its problem, when it has one, names why it is white.
	class RenderedMask
	{
	public:
		[[nodiscard]] RE::NiSourceTexture* Texture() const noexcept;
		[[nodiscard]] bool                 Animated() const noexcept { return animated_; }
		[[nodiscard]] bool                 Vector() const noexcept { return vector_; }
		[[nodiscard]] const std::string&   Problem() const noexcept { return problem_; }

	private:
		friend class Compositor;
		struct RefBinding
		{
			bool          isTexture = false;
			std::uint32_t texture = 0;  // into textures_
			std::string   signal;       // read per tick, otherwise
		};
		std::optional<Program>                           program_;
		std::vector<RefBinding>                          refs_;
		std::vector<PreparedSource>                      textures_;
		std::vector<std::shared_ptr<RenderedMask>>       dependencies_;
		std::vector<std::shared_ptr<TextureLab::Lookup>> curves_;
		std::shared_ptr<TextureLab::Target>              target_;
		bool                                             animated_ = false;
		bool                                             vector_ = false;
		bool                                             renderedOnce_ = false;
		std::uint64_t                                    renderedTick_ = 0;
		std::string                                      problem_;
	};

	struct PreparedLayer
	{
		const Layer*                        layer = nullptr;
		std::size_t                         index = 0;  // into the output's stack (file order), which is how the view names a layer
		std::optional<PreparedSource>       source;     // absent for a constant colour
		std::optional<PreparedMask>         mask;
		std::shared_ptr<TextureLab::Lookup> curve;      // the layer's curve, baked; null: identity
	};

	// The layers the view hides for one render (solo and mute), by file
	// index in ascending order. Empty is the common case and costs one
	// comparison per render; a hidden set is usually one or two entries, so
	// the lookup is a linear scan.
	struct LayerFilter
	{
		std::vector<std::size_t> hidden;

		[[nodiscard]] bool Hides(std::size_t a_index) const noexcept
		{
			for (const auto h : hidden) {
				if (h == a_index) {
					return true;
				}
			}
			return false;
		}
		[[nodiscard]] bool operator==(const LayerFilter&) const = default;
	};

	// One output's stack for one geometry, ready to render each tick.
	class RenderedStack
	{
	public:
		[[nodiscard]] RE::NiSourceTexture* Texture() const noexcept;  // the composite, or null before the first render
		[[nodiscard]] bool                 Animated() const noexcept { return animated_; }
		[[nodiscard]] std::uint32_t        Size() const noexcept { return size_; }
		[[nodiscard]] std::span<const PreparedLayer> Layers() const noexcept { return layers_; }
		[[nodiscard]] std::span<const Diagnostic>    Diagnostics() const noexcept { return diagnostics_; }

	private:
		friend class Compositor;
		std::vector<PreparedLayer>           layers_;
		RE::NiPointer<RE::NiSourceTexture>   base_;  // the material's own map for slots that edit one; null: black
		std::shared_ptr<TextureLab::Target>  neutral_;  // a height stack's 0.5 base when the material's displacement is flat; keeps base_ alive
		std::shared_ptr<TextureLab::Target>  target_;  // where the result lands; intermediates use the lab's scratch
		TextureLab::Target*                  latest_ = nullptr;
		std::uint32_t                        size_ = 0;
		bool                                 animated_ = false;
		bool                                 renderedOnce_ = false;
		LayerFilter                          filter_;  // what the last render hid; a static stack renders again when it changes
		std::vector<Diagnostic>              diagnostics_;
	};

	class Compositor
	{
	public:
		[[nodiscard]] static Compositor* GetSingleton();

		// Loads what the stack reads and acquires its targets. Null when the
		// lab is unavailable; problems with single layers are recorded on the
		// stack and the layer renders as white or is skipped.
		// A stack on a slot that edits an existing map (diffuse, normal, rmaos,
		// height) starts from that map and keeps its resolution up to a_maxSize;
		// the other slots start from black at a_size.
		[[nodiscard]] std::unique_ptr<RenderedStack> Prepare(const Recipe& a_recipe, const MaterialOutput& a_output, const GeometryInputs& a_inputs, std::uint32_t a_size, std::uint32_t a_maxSize);

		// Once per manager tick, so a mask shared by several stacks renders once.
		void BeginTick() noexcept { ++tick_; }

		// Renders the masks the stack reads, then every layer the filter does
		// not hide, with this tick's signal values. A static stack renders
		// once per filter: later calls with the same filter return at once.
		// With every layer hidden the composite is null, so the slot shows
		// its original map.
		void Render(RenderedStack& a_stack, const SignalState& a_signals, float a_time, const LayerFilter& a_filter);

		// Engine textures by path, shared with the engine's own loads.
		[[nodiscard]] RE::NiPointer<RE::NiSourceTexture> LoadImage(std::string_view a_path);

		// For the menu: one source or mask of the recipe as the compositor
		// would use it on this material, with its problem when it has one.
		[[nodiscard]] std::optional<PreparedSource> InspectSource(const Recipe& a_recipe, std::string_view a_name, const GeometryInputs& a_inputs);
		[[nodiscard]] std::optional<PreparedMask>   InspectMask(const Recipe& a_recipe, std::string_view a_name, const GeometryInputs& a_inputs);

	private:
		// A target filled with the neutral height 0.5, rendered once and shared:
		// CS offsets parallax by (height - 0.5) * scale, so a height stack over
		// a flat displacement map starts here and its masked layers displace
		// only where they are.
		std::shared_ptr<TextureLab::Target> NeutralHeight();
		std::shared_ptr<TextureLab::Target> neutralHeight_;

		std::optional<PreparedSource> PrepareSource(const Recipe& a_recipe, const Ref& a_ref, const GeometryInputs& a_inputs, std::uint32_t a_size, std::vector<Diagnostic>& a_out, const std::string& a_where);
		std::optional<PreparedMask>   PrepareMask(const Recipe& a_recipe, const Ref& a_ref, const GeometryInputs& a_inputs, std::uint32_t a_size, std::vector<Diagnostic>& a_out, const std::string& a_where);
		// The rendered mask of that name at that size, from the geometry's cache or freshly prepared; null when the recipe has no such mask.
		std::shared_ptr<RenderedMask> PrepareRenderedMask(const Recipe& a_recipe, std::string_view a_name, const GeometryInputs& a_inputs, std::uint32_t a_size, std::uint32_t a_depth);
		void                          RenderMask(RenderedMask& a_mask, const SignalState& a_signals, float a_time);
		// The geometry's mesh, read on first use; a problem when it cannot be.
		std::expected<std::shared_ptr<const MeshData>, std::string> MeshOf(const GeometryInputs& a_inputs);
		// Rasterises buffers into a cached target under a key, or returns the cached one.
		std::expected<std::shared_ptr<TextureLab::Target>, std::string> BakeInto(const GeometryInputs& a_inputs, const std::string& a_key, std::uint32_t a_size, const std::function<BakeBuffers()>& a_buffers);
		// The bake's target from the geometry's cache, or rasterised now; a problem when it cannot be.
		std::expected<std::shared_ptr<TextureLab::Target>, std::string> PrepareBake(const Source& a_source, const BakeSource& a_bake, const GeometryInputs& a_inputs, std::uint32_t a_size);
		std::expected<std::shared_ptr<TextureLab::Target>, std::string> PrepareDistance(const Source& a_source, const DistanceSource& a_distance, const GeometryInputs& a_inputs, std::uint32_t a_size);
		std::expected<std::shared_ptr<RenderedRipple>, std::string>     PrepareRipple(const Source& a_source, const RippleSource& a_ripple, const GeometryInputs& a_inputs, std::uint32_t a_size);
		void                                                            RenderRipple(RenderedRipple& a_ripple, const SignalState& a_signals, float a_time);
		std::shared_ptr<TextureLab::Lookup> BakeCurve(const Recipe& a_recipe, const CurveRef& a_curve, const std::optional<PreparedSource>& a_source, std::vector<Diagnostic>& a_out, const std::string& a_where);

		std::unordered_map<std::string, RE::NiPointer<RE::NiSourceTexture>> images_;
		std::uint64_t                                                       tick_ = 1;
	};
}
