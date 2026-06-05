#pragma once

#include <string>

#include "glm/ext/vector_float3.hpp"

enum class ShadingMode {
  Phong,
  Cel,
};

enum class OutlineMode {
  None,
  // Post-process Sobel: 3x3 luma + gNormal/gPosition probe in its own pass.
  // Most modular but reads the g-buffer one more time.
  Sobel,
  // 4-tap normal+depth probe folded into the deferred lighting pass.
  // Cheaper than Sobel and shares the gNormal/gPosition fetches already
  // happening in lighting, at the cost of coupling outline detection to
  // whichever lighting shader is active.
  Cross,
};

struct GraphicsSettings {
  // Camera
  float fovDegrees = 45.0f;
  float nearPlane = 0.1f;
  float farPlane = 500.0f;

  // Tonemap / bloom
  float exposure = 1.0f;
  // Tonemap operator: 0 = exponential `1-exp(-x)` (current/default), 1 = ACES
  // (Narkowicz), 2 = AgX. Applied in the tonemap pass before gamma.
  int tonemapMode = 0;
  bool bloomEnabled = false;
  float bloomThreshold = 1.0f;
  float bloomStrength = 1.0f;
  int bloomBlurIterations = 10;
  // Mip-chain (downsample/upsample) bloom instead of the fixed-iteration
  // full-res Gaussian ping-pong. false = current behavior. Wider, smoother
  // glow at lower cost; bloomBlurIterations is ignored when this is on.
  bool bloomMipChain = false;

  // SSAO
  bool ssaoEnabled = true;
  // 32 taps is visually ~indistinguishable from 64 at half-res but halves the
  // SSAO inner loop. Raise back toward 64 (the uniform array cap) for max
  // quality, or lower / disable in the debug panel for more FPS headroom.
  int ssaoKernelSize = 32;
  float ssaoRadius = 0.25f;
  float ssaoBias = 0.025f;
  // SSAO render resolution = renderWidth / ssaoScale (1 = full, 2 = half,
  // 4 = quarter). Lighting samples ssaoBlurColor through bilinear filtering
  // when scale > 1.
  int ssaoScale = 2;
  // Depth-aware (bilateral) SSAO blur instead of the plain 4x4 box. false =
  // current box blur. Removes occlusion haloing across depth discontinuities.
  bool ssaoBilateralBlur = true;
  // Exponent applied to the SSAO term (was a hardcoded 2.0). Higher = darker,
  // more contrasty AO. 2.0 reproduces current behavior.
  float ssaoPower = 2.0f;

  // FXAA
  bool fxaaEnabled = true;
  // FXAA quality: 0 = current minimal 3x3 blend, 1 = high (adds diagonal taps
  // + a stronger directional blend for cleaner long edges). Only used when
  // fxaaEnabled. (True SMAA needs its AreaTex/SearchTex LUT assets, which are
  // not bundled here — this is the no-asset upgrade in that slot.)
  int fxaaQuality = 0;
  // Ordered (Bayer 4x4) dither applied in the present pass to break up
  // HDR->8-bit banding on smooth gradients. 0 = off (current), 1 = ~1 LSB.
  float ditherStrength = 0.0f;

  // Top-left overlay showing ImGui's smoothed framerate.
  bool showFPS = true;
  // On-screen per-pass GPU timing HUD (reuses the gpu_profiler scopes). When
  // off, no GL timer queries are issued. false = current (off).
  bool showPerfHUD = false;

  // Camera-frustum cull the main G-buffer pass and the directional shadow pass
  // (point shadows already cull per face). The directional shadow pass is
  // submission-bound (re-submits all geometry once per cascade); per-cascade
  // culling rejects most chunks from the near cascades, so this is the primary
  // ShadowDir win. The map is split into per-node entities with bounds, so even
  // the terrain culls well.
  bool mainFrustumCulling = true;
  // Cache each entity's resolved Model* across frames to skip per-frame model-
  // key string building + map lookups in renderEntities. false = current path.
  bool cacheModelLookup = false;
  // Trilinear + anisotropic filtering on model textures. 1 = current
  // (nearest-mip / linear-mag, no anisotropy); >1 enables LINEAR_MIPMAP_LINEAR
  // with that anisotropy level (clamped to the GL max). Applied at load and on
  // change via a re-apply pass over loaded textures.
  int textureAnisotropy = 1;

  // Pixelation: render the entire 3D scene at fb/scale, then upscale with
  // GL_NEAREST in the present pass for a chunky-pixel look. 1 = off.
  int pixelationScale = 1;

  // Color restoration: desaturate fragments outside the local player's
  // shared::ColorBoundingBox in the tonemap pass. The server grows the box
  // as sections are completed, so "restored color" tracks game progress.
  bool colorRestorationEnabled = true;
  // 0 = no effect, 1 = full grayscale outside the box.
  float colorRestorationStrength = 1.0f;
  // Soft edge in world units to avoid a hard color/gray boundary.
  float colorRestorationEdgeWidth = 1.0f;
  // Colored point lights (fragments) keep their lit area in color even in
  // restored-to-grayscale regions. 0 = off, 1 = full.
  float colorRestorationLightStrength = 1.0f;

  // Shadows
  bool shadowsEnabled = true;
  bool pointShadowsEnabled = false;
  // Map sizes — changing triggers FBO/texture reallocation.
  int dirShadowMapSize = 4096;
  int pointShadowMapSize = 2048;
  // Cascaded shadow maps (directional). dirShadowMap is a texture array with
  // shared::kShadowCascadeCount layers; each cascade fits a view-frustum slice
  // for sharp near shadows and full coverage to the far plane.
  bool cascadedShadows =
      true;  // off → single stabilized map over shadowDistance
  // Cascades cover near..shadowDistance. Kept well under farPlane (500):
  // distant shadows are tiny on screen, and a tighter range means each
  // cascade's ortho frustum is smaller, so per-cascade culling rejects far more
  // geometry AND the same texels cover less area (crisper near shadows). Raise
  // toward farPlane if long-range shadows are needed.
  float shadowDistance =
      250.0f;  // clamped to farPlane; cascades cover near..this
  float shadowNearOffset =
      2.0f;  // split-scheme near, decoupled from camera near
  float cascadeSplitLambda = 0.7f;  // 0 = uniform splits, 1 = logarithmic
  float cascadeCasterPullback =
      50.0f;                      // light-space depth for off-slice casters
  float cascadeBlendBand = 0.0f;  // 0 = hard switch; fraction of range to blend
  bool visualizeCascades =
      false;  // tint each cascade band in the lighting pass
  float dirShadowPolyFactor = 2.0f;
  float dirShadowPolyUnits = 4.0f;
  // Point shadow projection + bias.
  float pointShadowFarPlane = 50.0f;
  float pointShadowPolyFactor = 2.0f;
  float pointShadowPolyUnits = 4.0f;
  // Directional-shadow PCF kernel half-width: 1 = current 3x3 (9 taps).
  // Higher = softer, more expensive shadow edges.
  int shadowPcfRadius = 1;
  // Multiplies the PCF tap spacing. 1.0 = current. Higher widens the penumbra
  // without raising the tap count.
  float shadowSoftness = 1.0f;
  // Diffuse alpha below this is discarded during shadow passes so cutout
  // meshes (foliage, fences) cast matching-shape shadows. 0 = no cutout.
  float shadowAlphaCutoff = 0.5f;

  // Shading
  ShadingMode shadingMode = ShadingMode::Phong;
  // 0 = no quantization, otherwise per-channel level count applied to the
  // material textures (albedo/specular/emissive) at G-buffer time.
  int textureQuantizeLevels = 0;
  // 0 = off, otherwise the size of a k-means palette built once from sampled
  // diffuse-texture pixels. When > 0, the G-buffer pass snaps albedo to the
  // nearest palette entry (linear-RGB nearest neighbour). Capped at
  // shared::kMaxPaletteColors.
  int paletteQuantizeColors = 0;
  // 0 = off, otherwise levels for HSV-V quantization of the final tonemapped
  // color in the present shader (after FXAA so band edges stay sharp).
  int postQuantizeLevels = 0;
  // 0 = off, otherwise HSV-V quantization applied to the skybox fragment
  // only, independent of texture/post quantize. Lets the sky posterize
  // without affecting scene geometry.
  int skyboxQuantizeLevels = 0;
  // 0 = off, otherwise k-means palette over sampled cubemap face pixels.
  // Same algorithm as paletteQuantizeColors but built per-skybox and bound
  // only by the skybox fragment shader. Capped at shared::kMaxPaletteColors.
  int skyboxPaletteColors = 0;
  // 0 = off, 1 = max softening. Bayer 4×4 dither offset applied before
  // skybox brightness quantize and palette snap, so hard plateau / region
  // boundaries break into a stippled transition instead of a sharp line.
  float skyboxSoftEdge = 0.0f;
  int celBands = 4;
  float celBandEpsilon = 0.02f;  // 0 = hard, ~0.1 = soft
  bool celHalfLambert = true;
  float celSpecularThreshold = 0.5f;
  float celSpecularEpsilon = 0.05f;
  bool celUseRampTexture = false;
  std::string celRampPath = "";  // empty → procedural
  // Stylized Fresnel rim light for the cel path only (Phong is untouched).
  // 0 = off (current). The rim is hard-edged (stepped) to match cel shading.
  float celRimStrength = 0.0f;
  glm::vec3 celRimColor{1.0f};
  float celRimPower = 4.0f;
  float celRimThreshold = 0.6f;

  // Barrier fog: season-tinted volumetric mist drawn at each active section
  // barrier (an invisible AABB wall) so blocked passages read as walls of fog
  // that lift when the season unlocks. Pure client-side post pass driven by the
  // replicated SectionBarrierTag — no effect when no barriers are active.
  bool fogEnabled = true;
  // Opacity accrued per world-unit of camera ray inside a barrier box.
  float fogDensity = 1.29f;
  // 1/units: how fast the mist thins above the fade-start height (0 = no fade,
  // uniform density up the whole wall).
  float fogHeightFalloff = 0.016f;
  // Scales the fog volume's vertical reach vs the barrier's (gameplay)
  // collision box, so the mist can rise higher than the wall.
  float fogHeightScale = 1.5f;
  // Fraction of the fog height that stays at full density before the upward
  // fade begins — 0.5 = solid lower half, fade the upper half, like a ground
  // mist bank covering the floor.
  float fogFadeStartFraction = 0.0f;
  // Anchor the height fade to the landscape under the fog (from scene depth)
  // instead of a flat z = 0, so the mist hugs the terrain. Off = barrier
  // center.
  bool fogFloorDetect = true;
  // World-space distance over which the mist fades in around the barrier
  // footprint, so it ramps up over a wide area instead of a hard wall edge.
  float fogWidth = 8.0f;
  // Ray-march samples across the fog span. Lower = faster, more banding (jitter
  // + noise hide most of it).
  int fogSteps = 12;
  // Fog render resolution = render size / fogScale (1 = full, 2 = half,
  // 4 = quarter), then composited back at full res. The main perf lever; fog is
  // low-frequency so quarter looks nearly identical.
  int fogScale = 4;
  // World-space frequency of the drifting mist noise (smaller = larger wisps).
  float fogNoiseScale = 0.072f;
  // Scroll speed of the mist animation.
  float fogNoiseSpeed = 2.0f;
  // 0 = uniform slab, 1 = fully noise-modulated wisps.
  float fogNoiseStrength = 1.0f;
  // Domain-warp amount: displaces the noise by a low-frequency flow so the mist
  // billows/curls like real fog instead of static lumps. 0 = off (cheapest).
  float fogSwirl = 0.0f;
  // Breaks the barrier box's flat faces / straight silhouette edges into wisps
  // by carving the surface inward with noise. 0 = hard box edges, higher =
  // softer and more mist-like (scaled by the box's thinnest half-extent).
  float fogMistiness = 0.0f;
  // Self-shadowed volumetric shading: a few density taps toward the sun darken
  // the mist's depths so it reads as a 3D volume instead of a flat tint.
  bool fogLighting = true;
  // Shadow-floor brightness for the lit path (lower = darker depths / more 3D
  // contrast; 1 = no self-shadowing).
  float fogAmbient = 0.4f;
  // Directional in-scattering: adds a bright sun halo on top of the shading.
  // 0 = no halo.
  float fogScatterStrength = 0.0f;
  // Henyey-Greenstein anisotropy (forward-scatter bias) for the sun halo.
  float fogScatterAnisotropy = 0.6f;
  // Color of the in-scattered sunlight added on top of the season tint.
  glm::vec3 fogScatterColor{1.0f, 0.96f, 0.88f};

  // Outlines (post-process Sobel only)
  OutlineMode outlineMode = OutlineMode::Cross;
  glm::vec3 outlineColor{0.0f};
  // Depth threshold is relative (Δd / d), normal threshold is sum of 4
  // neighbor (1 - dot(n, nNeighbor)).
  float outlineSobelWidth = 1.0f;
  float outlineDepthThreshold = 0.05f;
  float outlineNormalThreshold = 0.5f;

  // Image-based-ambient approximation: tint the global ambient by the current
  // skybox's average color, modulated by world-up so sky-facing surfaces read
  // brighter. 0 = current flat ambient (no change). A robust, orientation-free
  // stand-in for full SH irradiance IBL.
  float iblAmbientStrength = 0.0f;

  // Directional-light override (off → scene/ECS values used)
  bool overrideDirLight = false;
  glm::vec3 dirLightDirection{0.3f, 1.0f, -0.4f};
  glm::vec3 dirLightAmbient{0.2f, 0.2f, 0.2f};
  glm::vec3 dirLightDiffuse{0.8f, 0.8f, 0.8f};
  glm::vec3 dirLightSpecular{1.0f, 1.0f, 1.0f};
};

enum class GraphicsPreset {
  Default,
  HighQuality,
  Performance,
  CelShaded,
  // Parallel "(New)" presets: each mirrors the base preset above and then
  // opts into the new toggleable graphics features tuned for that tier. The
  // base presets and per-field defaults are never changed, so selecting a
  // base preset reproduces today's look exactly.
  DefaultNew,
  HighQualityNew,
  PerformanceNew,
  CelShadedNew,
  Count,
};

void applyPreset(GraphicsSettings& s, GraphicsPreset p);
const char* presetName(GraphicsPreset p);
