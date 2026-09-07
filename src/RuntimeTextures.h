#pragma once

#include "Analysis.h"
#include "Expression.h"
#include "Mesh.h"
#include "PCH.h"
#include "TextureSize.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <span>
#include <unordered_set>
#include <utility>
#include <vector>

namespace WornEnchantmentPBR
{
	// GPU-generated layer textures. Each Target is an NiSourceTexture shell
	// (loaded through the engine from a placeholder DDS) whose renderer data
	// points at a render target we own, so Community Shaders binds it like any
	// other material texture while a per-tick pass rewrites its content.
	class TextureLab
	{
	public:
		enum class Mode : std::uint32_t
		{
			kGlow = 0,       // rgb from the source, alpha 1
			kSheen = 1,      // rgb 1, alpha = source luminance (fuzz map)
			kHeight = 2,     // armor relief + source luminance (displacement)
			kRoughness = 3,  // the armor's RMAOS with roughness lowered by the source
			kChannel = 4,    // inspector: one channel (or rgb) of the armor input, raw UV
			kMaskedGlow = 5, // glow multiplied per texel by a mask from one armor channel
			kLayer = 6,      // compositor: one layer blended over the previous result
			kCopy = 7,       // the source's four channels at the given mip, raw placement (the sample readback)
		};

		enum class ArmorInput : std::uint32_t
		{
			kNone = 0,
			kDisplacementR = 1,  // the material's own height map
			kOcclusionB = 2,     // RMAOS blue (AO) as a relief proxy: recesses are dark
			kRmaos = 3,          // the whole RMAOS texture (roughness mode)
			kNormalSlope = 4,    // relief from the normal map's slope: grooves and edges read low
			kDiffuseLuma = 5,    // relief from diffuse luminance: dark reads low
		};

		// How the scrolling source is placed; shared by every mode.
		struct Scroll
		{
			float uOffset = 0.0f;
			float vOffset = 0.0f;
			float tileU = 1.0f;
			float tileV = 1.0f;
			bool  mirrorU = false;
			bool  mirrorV = false;
			bool  transpose = false;
			float sourceMip = 0.0f;  // sample the source at this mip (softens)
		};

		// Second input, sampled with the raw mesh UV (no tiling or scroll).
		struct ArmorSource
		{
			RE::NiSourceTexture* texture = nullptr;
			ArmorInput           input = ArmorInput::kNone;
		};

		// height = 0.5 + (relief - reliefMean) * armorWeight * reliefContrast
		//              + (noise - noiseMean) * noiseWeight
		struct HeightParams
		{
			float armorWeight = 1.0f;    // the armor's relief
			float reliefContrast = 3.0f; // stretches the relief around its mean (AO maps are nearly flat)
			float reliefMean = 0.5f;     // measured from the depth input's channel
			float noiseWeight = 0.35f;   // the scrolling source on top
			float noiseMean = 0.5f;      // measured from the source
		};

		struct RoughnessParams
		{
			float strength = 0.25f;  // roughness drop at the source's peaks (0..1)
			float contrast = 2.0f;   // sharpens the source into patches
		};

		// 0..3 = r, g, b, a as grey; 4 = rgb; 5 = normal slope relief; 6 = diffuse luminance.
		struct ChannelParams
		{
			std::uint32_t channel = 4;
		};

		// mask = channel of the armor input; above threshold (smoothstep with
		// softness) when threshold > 0; inverted on request; strength lerps
		// between no mask (0) and the full mask (1).
		struct MaskParams
		{
			std::uint32_t channel = 1;  // 0 r, 1 g, 2 b, 3 a
			float         threshold = 0.0f;
			float         softness = 0.1f;
			bool          invert = false;
			float         strength = 1.0f;
		};

		// How the compositor samples one source: which channel, and whether the
		// image is placed by its transform (tiled) or by the mesh's own UV.
		struct LayerInput
		{
			std::uint32_t channel = 4;  // 0..3 = r, g, b, a; 4 = rgb; 5 = luminance
			bool          meshSpace = false;
			Scroll        transform;
		};

		// A 256-entry table a layer pass applies to its source value: a curve
		// baked on the CPU, sampled by the value on the GPU.
		class Lookup
		{
		public:
			~Lookup();
			REX::W32::ID3D11Texture2D*          texture = nullptr;
			REX::W32::ID3D11ShaderResourceView* srv = nullptr;
		};

		// The interpreter pass: one fixed pixel shader runs a Program per texel.
		// A reference is either a texture read (an image sampled by its own
		// placement and channel) or a value (a signal, the same at every texel).
		inline static constexpr std::uint32_t kProgramTextures = 8;
		inline static constexpr std::uint32_t kProgramRefs = 16;
		inline static constexpr std::uint32_t kProgramCurves = 4;
		inline static constexpr std::uint32_t kProgramStack = 32;

		struct ProgramTexture
		{
			RE::NiSourceTexture* texture = nullptr;
			LayerInput           sampling;
			float                normalize = 1.0f;
		};
		struct ProgramRef
		{
			bool          isTexture = false;
			std::uint32_t texture = 0;  // into textures, when isTexture
			Vec3          value{};      // broadcast, otherwise
		};
		// Fixed storage, filled per tick without allocating: the compositor
		// refuses a mask at preparation when it reads more names, images or
		// curves than these arrays hold, so the counts never exceed the sizes.
		// A count above the size is a bug in the caller and RenderProgram
		// refuses the pass rather than read past the array.
		struct ProgramPass
		{
			std::span<const Program::Node>              code;  // at most kMaxExpressionOps
			std::array<ProgramRef, kProgramRefs>        refs{};  // one per Program::References()
			std::uint32_t                               refCount = 0;
			std::array<ProgramTexture, kProgramTextures> textures{};
			std::uint32_t                               textureCount = 0;
			std::array<const Lookup*, kProgramCurves>   curves{};  // one per Program::Curves()
			std::uint32_t                               curveCount = 0;
			float                                       time = 0.0f;
			bool                                        vectorResult = false;  // false: the scalar result fills rgb
		};

		// The ripple pass: fronts expanding over the surface from each live
		// firing's origin, read against the geometry's position bake.
		inline static constexpr std::uint32_t kRippleFirings = 8;
		struct RippleFiring
		{
			Vec3  origin;  // bind-pose units, the same frame as the position bake
			float age = 0.0f;  // seconds since the firing
		};
		// Fixed storage as ProgramPass: the compositor keeps the first
		// kRippleFirings live firings, so firingCount never exceeds the array.
		struct RipplePass
		{
			RE::NiSourceTexture*                      positions = nullptr;  // the position bake
			float                                     frame = 128.0f;      // the bake's half-range in units
			std::array<RippleFiring, kRippleFirings>  firings{};
			std::uint32_t                             firingCount = 0;
			float                                     speed = 100.0f;      // units per second
			float                                     width = 10.0f;       // units
			float                                     decay = 1.0f;        // per second of age
			bool                                      disc = false;        // false: ring
		};

		// The compositor's generic pass: result = blend(previous, source x colour)
		// applied by opacity x mask on the selected channels.
		struct LayerPass
		{
			RE::NiSourceTexture* previous = nullptr;  // null: black
			RE::NiSourceTexture* source = nullptr;    // null: the constant colour alone
			LayerInput           input;
			float                normalize = 1.0f;  // multiplies the source (0.5 / mean luminance for colour fields)
			float                color[3]{ 1.0f, 1.0f, 1.0f };
			float                opacity = 1.0f;
			std::uint32_t        blend = 0;  // 0 replace, 1 multiply, 2 add, 3 subtract, 4 screen, 5 lerp
			std::uint32_t        channels = 15;  // bit 0 r, 1 g, 2 b, 3 a
			RE::NiSourceTexture* mask = nullptr;  // sampled by mesh UV; null: no mask
			std::uint32_t        maskChannel = 0;  // 0..3 = r, g, b, a; 5 = luminance
			const Lookup*        curve = nullptr;  // applied to the source value per channel; null: identity
		};

		struct LayerParams
		{
			Mode            mode = Mode::kGlow;
			Scroll          scroll;
			ArmorSource     armor;
			HeightParams    height;
			RoughnessParams roughness;
			ChannelParams   channel;
			MaskParams      mask;
			LayerPass       layer;
		};

		class Target
		{
		public:
			~Target();
			[[nodiscard]] RE::NiSourceTexture* Texture() const noexcept { return shell.get(); }

			RE::NiPointer<RE::NiSourceTexture>  shell;
			RE::NiTexture::RendererData*        originalData = nullptr;
			RE::NiTexture::RendererData*        ourData = nullptr;
			REX::W32::ID3D11Texture2D*          texture = nullptr;
			REX::W32::ID3D11ShaderResourceView* srv = nullptr;
			REX::W32::ID3D11RenderTargetView*   rtv = nullptr;
			std::uint32_t                       size = 0;
		};

		[[nodiscard]] static TextureLab* GetSingleton();

		// Compiles the shaders and creates the pipeline objects. Safe to call
		// more than once; returns whether the lab is usable.
		bool Init();
		[[nodiscard]] bool Available() const noexcept { return available_; }

		// A fresh or pooled target of a_size x a_size, or null on failure. A
		// pooled target keeps its last content: every pass writes every texel.
		std::shared_ptr<Target> Acquire(TextureSize a_size);

		// Rewrites the target's mip chain from a_source with the given params.
		bool Render(Target& a_target, RE::NiSourceTexture* a_source, const LayerParams& a_params);
		// Runs a program per texel into the target.
		bool RenderProgram(Target& a_target, const ProgramPass& a_pass);
		[[nodiscard]] bool InterpreterAvailable() const noexcept { return programPs_ != nullptr; }

		// Rasterises baked triangles (UV as position) into the target, black elsewhere.
		bool BakeMesh(Target& a_target, const BakeBuffers& a_bake);
		// Renders the ripple fronts into the target; no firings clears it.
		bool RenderRipple(Target& a_target, const RipplePass& a_pass);
		[[nodiscard]] bool RippleAvailable() const noexcept { return ripplePs_ != nullptr; }

		// A low mip of the RMAOS and diffuse maps read back to the CPU: each
		// map rendered at the mip that fits kSampleSide into a kSampleSide
		// target, so the sample is a mip average, never kMaxSampleTexels
		// texels or more. Game thread (a staging map). Nothing when either
		// map is null or not resident or the readback fails; each such
		// texture is warned about once.
		inline static constexpr std::uint32_t kSampleSide = 64;
		[[nodiscard]] std::optional<MaterialSample> SampleMaterial(RE::NiSourceTexture* a_rmaos, RE::NiSourceTexture* a_diffuse);

		// The classify pass: per texel, the RMAOS and diffuse maps at the
		// mesh UV go to the nearest of the analysis' centroids under its
		// weights, written as id / 255 grey; agrees with NearestCluster. Refused
		// when the analysis holds more than kMaxClusters clusters.
		bool RenderClusters(Target& a_target, RE::NiSourceTexture* a_rmaos, RE::NiSourceTexture* a_diffuse, const MaterialAnalysis& a_analysis);
		[[nodiscard]] bool ClassifyAvailable() const noexcept { return classifyPs_ != nullptr; }
		[[nodiscard]] bool BakingAvailable() const noexcept { return bakeVs_ != nullptr && bakeLayout_ != nullptr; }

		// The first a_bytes of a GPU buffer, through a staging copy; empty on failure.
		[[nodiscard]] std::vector<std::uint8_t> ReadBuffer(REX::W32::ID3D11Buffer* a_buffer, std::uint32_t a_bytes);

		// A lookup from 256 values over 0..1; null when the device is unavailable.
		std::shared_ptr<Lookup> CreateLookup(std::span<const float, 256> a_values);

		// Width and height of a resident texture from its D3D resource. The engine's
		// renderer record carries 0x0 for streamed maps, so it is not consulted.
		// Empty when the texture is null, not resident, or not a 2D texture.
		struct Extent
		{
			std::uint32_t width = 0;
			std::uint32_t height = 0;
		};
		static std::optional<Extent> ExtentOf(RE::NiSourceTexture* a_source);

		// Mean luminance of a_source's RGB (0..1) via its 1x1 mip; cached per texture.
		float MeanLuminance(RE::NiSourceTexture* a_source);
		// Mean of one channel (0..3) of a_source, same method; cached.
		float MeanChannel(RE::NiSourceTexture* a_source, std::uint32_t a_channel);

		// Inspector thumbnails: a small target showing one channel of a_source.
		// The render thread asks (Preview) and gets the last finished target,
		// or null until one exists; the game thread renders what was asked for
		// (RenderPreviews, once per tick). The immediate context is used on the
		// game thread only. Static sources render once per generation; dynamic
		// ones (our own targets) every tick they are asked for.
		std::shared_ptr<Target> Preview(RE::NiSourceTexture* a_source, std::uint32_t a_channel, bool a_dynamic);
		void                    RenderPreviews();
		void                    ClearPreviews();
		// Game thread, after an apply or retire: pooled targets change hands,
		// so a cached preview of a target's texture may now show another
		// stack. Every preview asked for again is re-rendered; the rest are
		// dropped.
		void                    InvalidatePreviews() noexcept { previewGeneration_.fetch_add(1, std::memory_order_relaxed); }

		// One scratch target per size, shared by every stack for its
		// intermediate layers; a stack owns only the target its result lands in.
		[[nodiscard]] Target* Scratch(TextureSize a_size);

		// Drops pooled targets; live shared_ptrs stay valid.
		void Clear();

	private:
		struct SavedState;

		bool CompileShaders();
		bool CreateTarget(Target& a_target, TextureSize a_size);
		RE::NiPointer<RE::NiSourceTexture> LoadShell();
		void Recycle(Target* a_target);

		bool                                            available_ = false;
		bool                                            initTried_ = false;
		REX::W32::ID3D11Device*                         device_ = nullptr;
		REX::W32::ID3D11DeviceContext*                  context_ = nullptr;
		REX::W32::ID3D11VertexShader*                   vs_ = nullptr;
		REX::W32::ID3D11PixelShader*                    ps_ = nullptr;
		REX::W32::ID3D11PixelShader*                    programPs_ = nullptr;
		REX::W32::ID3D11VertexShader*                   bakeVs_ = nullptr;
		REX::W32::ID3D11PixelShader*                    bakePs_ = nullptr;
		REX::W32::ID3D11InputLayout*                    bakeLayout_ = nullptr;
		REX::W32::ID3D11PixelShader*                    ripplePs_ = nullptr;
		REX::W32::ID3D11Buffer*                         rippleConstants_ = nullptr;
		REX::W32::ID3D11PixelShader*                    classifyPs_ = nullptr;
		REX::W32::ID3D11Buffer*                         classifyConstants_ = nullptr;
		REX::W32::ID3D11Buffer*                         constants_ = nullptr;
		REX::W32::ID3D11Buffer*                         programConstants_ = nullptr;
		REX::W32::ID3D11SamplerState*                   sampler_ = nullptr;
		REX::W32::ID3D11BlendState*                     blend_ = nullptr;
		REX::W32::ID3D11DepthStencilState*              depth_ = nullptr;
		REX::W32::ID3D11RasterizerState*                raster_ = nullptr;
		std::uint32_t                                   nextShell_ = 0;
		std::vector<std::unique_ptr<Target>>            pool_;
		std::map<std::uint32_t, std::shared_ptr<Target>> scratch_;
		std::unordered_map<RE::NiSourceTexture*, float> luminance_;
		std::map<std::pair<RE::NiSourceTexture*, std::uint32_t>, float> channelMeans_;
		std::unordered_set<RE::NiSourceTexture*>        sampleWarned_;  // textures SampleMaterial has already warned about

		std::optional<float> ReadBackMean(Target& a_target);
		// Every texel of the target's top mip as packed RGBA8, through a
		// staging copy; empty on failure or a format other than the lab's own.
		std::vector<std::uint8_t> ReadBackPixels(Target& a_target);
		using PreviewKey = std::pair<RE::NiSourceTexture*, std::uint32_t>;
		struct PreviewEntry
		{
			std::shared_ptr<Target> target;   // null until the game thread has rendered it
			std::uint64_t           generation = 0;  // the generation it was last rendered in
			bool                    dynamic = false;
			bool                    wanted = false;  // asked for since the last RenderPreviews
		};
		// previews_ and previewRequests_ are shared by the two threads under
		// previewLock_; targets are created, rendered and freed on the game
		// thread only. A freed target lingers in the graveyard for a moment so
		// a draw list the render thread already built can still present it.
		std::mutex                                            previewLock_;
		std::map<PreviewKey, PreviewEntry>                    previews_;
		std::atomic<std::uint64_t>                            previewGeneration_{ 1 };
		std::uint64_t                                         previewSeen_ = 1;  // game thread: the generation previews_ was last purged for
		std::vector<std::pair<std::shared_ptr<Target>, std::uint64_t>> previewGraveyard_;  // target, tick it was retired
		std::uint64_t                                         previewTick_ = 0;
	};
}
