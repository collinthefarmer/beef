# Compatibility profiles — 2026-09-24

The initial candidate targets Steam Skyrim 1.6.1170 with minimum SKSE 2.2.6.
This anchors the previously recorded runtime environment; it is not acceptance
of the current DLL. The [SKSE 2.2.6 version header](https://raw.githubusercontent.com/ianpatt/skse64/v2.2.6/skse64_common/skse_version.h)
records that runtime pairing. The [official download page](https://skse.silverlock.org/)
now lists Steam 1.7.104 / SKSE 2.3.1; this candidate does not accept that runtime.

## Source audit

- CommonLib is pinned at b93280e832f263dbef44e44cbe2936622a02f91a.
  SE/AE compile branches remain enabled and VR remains disabled.
- Player Update uses vtable slot 0xAD. Light code has SE/AE relocation pairs.
  SkinPalette mirrors flattened-bone storage offsets (0x130/0x158 and
  0x128/0x150), stride 0x80, and world-transform offset 0x34.
  Address Library does not verify these layout assumptions.
- PBRMaterial mirrors Community Shaders material layout. The vendored source
  baseline is CS 1.8.3, revision dd2677fc4020db1da91b39cebef3dbfbe8913983.
  TruePBR is required. Header fingerprints detect source/profile drift, not ABI
  compatibility with an installed peer.
- Menu header baseline is 3.13.0, revision
  c97cdce6dd207c7cf1401611bc82bd7e8f97a814. Export checks establish API presence,
  not ABI compatibility. The framework is required for the editor.
- Address Library AE is required; its exact package version remains unknown.

## Implemented contract

One JSON profile drives dependency pins, compiled families, an explicit loader
runtime whitelist, minimum SKSE, startup guard, package label, and build identity.
The startup guard runs before SKSE initialization and hooks, including when an
older loader uses CommonLib's permissive legacy Query export.

CommonLib's `StructCompatibility::Independent` describes the pre/post-1.6.629
structure boundary; it makes no claim about Community Shaders structures.
An explicit loader whitelist replaces the broad AddressLibrary loader mode,
while Address Library remains necessary for the plugin's relocations.

Both archives carry the effective profile. Packaging and archive verification
reject inconsistent identity/profile/declaration metadata. These are consistency
checks, not independent proof that a DLL contains the claimed source. Preserve
archive checksums and symbols alongside runtime evidence.

Only one production profile is committed. Tests exercise an alternate SE profile
without claiming that configuration is supported. New profiles need a source
review, dependency/license review if pins change, a separate build directory,
and runtime acceptance. `runtime_verified` stays false in build profiles;
acceptance belongs to evidence for an exact candidate and installed peer matrix.

## Offline validation

Windows Release DLL and candidate archives built successfully. CTest passed
compatibility, package, license, build, generated-output, and recipe-contract
suites. Coverage includes native compilation of the generated runtime guard,
alternate-profile CMake generation, configuration-sensitive identity, stable
regeneration, malformed constraints, vendored-header drift, and package metadata
mismatches (including rehashed inconsistent payloads). Targeted clang-tidy of
`src/main.cpp` reported zero findings. Both candidate archives passed the package
verifier. No game run, installation, or publication was performed.
