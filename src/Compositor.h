#pragma once

// Sources to textures. For one geometry and one material output, the
// compositor loads the sources the stack reads, renders the layers in order
// through the lab's layer pass, and hands back the texture the binding
// puts in the slot: image fields, constant colours, material channels
// (the normal map's slope rendered as a map of its own), masks that are
// one material channel, and mask expressions through the interpreter
// pass; a mask that cannot be prepared evaluates as white with a
// diagnostic on the row.

#include "Analysis.h"
#include "Mesh.h"
#include "MeshReader.h"
#include "PBRMaterial.h"
#include "PCH.h"
#include "Recipe.h"
#include "RuntimeTextures.h"
#include "Signals.h"

#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace WornEnchantmentPBR
{
	// The material channels a stack can read, taken from the geometry's own
	// material at apply time (before any slot is swapped). From measures the
	// displacement map and samples the RMAOS and diffuse maps through the
	// lab, so it runs on the game thread; every later reader of the record,
	// the snapshot included, has no render or readback to do.
	struct MaterialInputs
	{
		RE::NiPointer<RE::NiSourceTexture>      diffuse;
		RE::NiPointer<RE::NiSourceTexture>      normal;
		RE::NiPointer<RE::NiSourceTexture>      rmaos;
		RE::NiPointer<RE::NiSourceTexture>      displacement;
		bool                                    flatDisplacement = true;  // no real displacement map, or one whose mean sits at either end (NOTES 46)

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
		ShaderChannel                      channel = ShaderChannel::kR;
		bool                               animated = false;
		std::string                        problem;
		std::shared_ptr<RenderedMask>      rendered;
	};

	// Rendered masks per geometry, keyed "<mask>@<size>", shared by every
	// stack on the geometry that reads them.
	using MaskCache = std::unordered_map<std::string, std::shared_ptr<RenderedMask>>;
	using RippleCache = std::unordered_map<std::string, std::shared_ptr<RenderedRipple>>;

	// What the compositor reads on one geometry for one apply: its material's
	// maps, and the masks and ripples rendered for it. The mesh and its bakes
	// live in the compositor's MeshCache, keyed by the geometry, so they
	// outlive the apply.
	// Maps computed from the material's own: the normal map's slope, rendered
	// once per apply on the first source that reads it (the game thread);
	// until then an inspection reports it unrendered. The cluster map is the
	// material analysis classified per texel (RenderClusterMap), rendered
	// on the first source that reads it and again when a source asks for
	// other settings.
	struct DerivedMaps
	{
		std::shared_ptr<TextureLab::RenderTarget> normalSlope;
		std::string                         problem;  // why normalSlope stayed null after a try
		bool                                tried = false;
		std::shared_ptr<TextureLab::RenderTarget> clusters;         // id / 255 grey, one cluster id per texel
		ClusterSettings                     clusterSettings;  // what clusters was rendered with
		std::string                         clustersProblem;  // why clusters stayed null after a try
		bool                                clustersTried = false;
	};

	struct GeometryInputs
	{
		MaterialInputs                    material;
		RE::NiPointer<RE::BSGeometry>     geometry;  // for the bakes; null when unknown
		RE::NiPointer<RE::NiAVObject>     root;      // the actor's 3D root: the frame of the bind pose, and where nodes are looked up
		std::shared_ptr<MaskCache>        masks = std::make_shared<MaskCache>();
		std::shared_ptr<RippleCache>      ripples = std::make_shared<RippleCache>();
		std::shared_ptr<DerivedMaps>      derived = std::make_shared<DerivedMaps>();
	};

	// Fronts expanding from a trigger's live firings over the position bake,
	// rendered each tick into its own target.
	class RenderedRipple
	{
	public:
		[[nodiscard]] RE::NiSourceTexture* Texture() const noexcept;

	private:
		friend class Compositor;
		std::shared_ptr<TextureLab::RenderTarget> target_;
		std::shared_ptr<TextureLab::RenderTarget> positions_;  // the geometry's position bake
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
		std::shared_ptr<TextureLab::RenderTarget>              target_;
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
		[[nodiscard]] TextureSize          Size() const noexcept { return size_; }
		[[nodiscard]] std::span<const PreparedLayer> Layers() const noexcept { return layers_; }
		[[nodiscard]] std::span<const Diagnostic>    Diagnostics() const noexcept { return diagnostics_; }

	private:
		friend class Compositor;
		std::vector<PreparedLayer>           layers_;
		RE::NiPointer<RE::NiSourceTexture>   base_;  // the material's own map for slots that edit one; null: black
		std::shared_ptr<TextureLab::RenderTarget>  neutral_;  // a height stack's 0.5 base when the material's displacement is flat; keeps base_ alive
		std::shared_ptr<TextureLab::RenderTarget>  target_;  // where the result lands; intermediates use the lab's scratch
		TextureLab::RenderTarget*                  latest_ = nullptr;
		TextureSize                          size_ = TextureSize::Clamp(TextureSize::kMin);
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
		[[nodiscard]] std::unique_ptr<RenderedStack> Prepare(const Recipe& a_recipe, const SurfaceOutput& a_output, const GeometryInputs& a_inputs, TextureSize a_size, TextureSize a_maxSize);

		// Once per manager tick, so a mask shared by several stacks renders
		// once; the clock stamps the mesh cache's use.
		void BeginTick(std::uint32_t a_nowMS) noexcept
		{
			++tick_;
			nowMS_ = a_nowMS;
		}

		// Renders the masks the stack reads, then every layer the filter does
		// not hide, with this tick's signal values. A static stack renders
		// once per filter: later calls with the same filter return at once.
		// With every layer hidden the composite is null, so the slot shows
		// its original map.
		void Render(RenderedStack& a_stack, const SignalState& a_signals, float a_time, const LayerFilter& a_filter);

		// Engine textures by path, shared with the engine's own loads.
		[[nodiscard]] RE::NiPointer<RE::NiSourceTexture> LoadImage(std::string_view a_path);

		// For the snapshot, from the render thread: one source or mask of the
		// recipe as a stack on this geometry rendered it, read from the caches
		// alone (the geometry's masks and ripples, the mesh entry's bakes, the
		// images loaded so far, the material's own maps). Nothing is loaded,
		// read, baked or rendered here (NOTES 53); a row nothing has rendered
		// reports kNotRendered.
		static constexpr std::string_view kNotRendered = "not rendered on this geometry; add a layer that reads it";
		[[nodiscard]] std::optional<PreparedSource> InspectSource(const Recipe& a_recipe, std::string_view a_name, const GeometryInputs& a_inputs) const;
		[[nodiscard]] std::optional<PreparedMask>   InspectMask(const Recipe& a_recipe, std::string_view a_name, const GeometryInputs& a_inputs) const;

		// The geometry's mesh through the cache: read on the first call,
		// the entry from then on; a problem string when it cannot be read.
		// Game thread.
		[[nodiscard]] std::expected<std::shared_ptr<MeshEntry>, std::string> MeshOf(RE::BSGeometry* a_geometry);
		// The entry as it stands, or null; never reads. Any thread (Threads in ARCHITECTURE.md).
		[[nodiscard]] std::shared_ptr<const MeshEntry> CachedMesh(RE::BSGeometry* a_geometry) const noexcept;
		// Game thread, from the tick: every kMeshSweepMS, entries unused for
		// kMeshMaxAgeMS whose geometry is not among a_bound are dropped.
		static constexpr std::uint32_t kMeshSweepMS = 5000;
		static constexpr std::uint32_t kMeshMaxAgeMS = 30000;
		[[nodiscard]] bool             MeshSweepDue(std::uint32_t a_nowMS) const noexcept { return a_nowMS - lastSweepMS_ >= kMeshSweepMS; }
		void                           SweepMeshes(std::uint32_t a_nowMS, std::span<RE::BSGeometry* const> a_bound);
		void                           ClearMeshes() noexcept;

		// The material's sample and its clusters at the default settings,
		// read back once per pair of maps and kept for the session. The
		// readback stalls the game thread on the GPU, so it never runs at
		// apply: Paint's read of a geometry asks for it (RequestMesh), and a
		// recipe whose source is `materialClusters` asks at prepare. Cached
		// only reads. Game thread.
		struct MaterialRecord
		{
			RE::NiPointer<RE::NiSourceTexture>      rmaos;
			RE::NiPointer<RE::NiSourceTexture>      diffuse;
			std::shared_ptr<const MaterialSample>   sample;
			std::shared_ptr<const MaterialAnalysis> analysis;  // ClusterMaterial over the sample at ClusterSettings{}
			std::string                             problem;   // why sample is null after a try
		};
		[[nodiscard]] const MaterialRecord& AnalyseMaterial(const MaterialInputs& a_material);
		[[nodiscard]] const MaterialRecord* CachedMaterial(const MaterialInputs& a_material) const noexcept;
		void                                ClearMaterials() noexcept;

	private:
		// A target filled with the neutral height 0.5, rendered once and shared:
		// CS offsets parallax by (height - 0.5) * scale, so a height stack over
		// a flat displacement map starts here and its masked layers displace
		// only where they are.
		std::shared_ptr<TextureLab::RenderTarget> NeutralHeight();
		std::shared_ptr<TextureLab::RenderTarget> neutralHeight_;

		std::optional<PreparedSource> PrepareSource(const Recipe& a_recipe, const Ref& a_ref, const GeometryInputs& a_inputs, TextureSize a_size, std::vector<Diagnostic>& a_out, const std::string& a_where, std::uint32_t a_depth = 0);
		std::optional<PreparedMask>   PrepareMask(const Recipe& a_recipe, const Ref& a_ref, const GeometryInputs& a_inputs, TextureSize a_size, std::vector<Diagnostic>& a_out, const std::string& a_where);
		// The rendered mask of that name at that size, from the geometry's cache or freshly prepared; null when the recipe has no such mask.
		std::shared_ptr<RenderedMask> PrepareRenderedMask(const Recipe& a_recipe, std::string_view a_name, const GeometryInputs& a_inputs, TextureSize a_size, std::uint32_t a_depth);
		void                          RenderMask(RenderedMask& a_mask, const SignalState& a_signals, float a_time);
		// Rasterises buffers into a target kept on the mesh entry under a key
		// ("<definition>@<size>", Mesh.h), or returns the one already there.
		std::expected<std::shared_ptr<TextureLab::RenderTarget>, std::string> BakeInto(MeshEntry& a_entry, const std::string& a_key, TextureSize a_size, const std::function<BakeBuffers()>& a_buffers);
		// The bake's target from the mesh entry, or rasterised now; a problem when it cannot be.
		std::expected<std::shared_ptr<TextureLab::RenderTarget>, std::string> PrepareBake(const BakeSource& a_bake, const GeometryInputs& a_inputs, TextureSize a_size);
		std::expected<std::shared_ptr<TextureLab::RenderTarget>, std::string> PrepareDistance(const DistanceSource& a_distance, const GeometryInputs& a_inputs, TextureSize a_size);
		std::expected<std::shared_ptr<RenderedRipple>, std::string>     PrepareRipple(const Source& a_source, const RippleSource& a_ripple, const GeometryInputs& a_inputs, TextureSize a_size);
		void                                                            RenderRipple(RenderedRipple& a_ripple, const SignalState& a_signals, float a_time);
		std::shared_ptr<TextureLab::Lookup> BakeCurve(const Recipe& a_recipe, const CurveRef& a_curve, const std::optional<PreparedSource>& a_source, std::vector<Diagnostic>& a_out, const std::string& a_where);

		std::unordered_map<std::string, RE::NiPointer<RE::NiSourceTexture>> images_;
		MeshCache                                                           meshes_;
		std::map<std::pair<RE::NiSourceTexture*, RE::NiSourceTexture*>, MaterialRecord> materials_;  // by the RMAOS and diffuse maps
		std::uint64_t                                                       tick_ = 1;
		std::uint32_t                                                       nowMS_ = 0;
		std::uint32_t                                                       lastSweepMS_ = 0;
	};
}
