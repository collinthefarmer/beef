# Third-party notices

Original BetterEnchantmentEffects material is GPL-3.0-only with the additional
permission in [COPYING.md](COPYING.md). The GPL text is in [LICENSE](LICENSE).
Third-party material retains its own terms. This inventory covers the source
and local candidate payload reviewed on 2026-09-23; it does not certify runtime
compatibility or completion of source-publication requirements.

The [license inventory](licenses/inventory.json) records upstream locations,
SHA-256 hashes of notice files and unchanged vendored headers, and reviewed
CMake dependency pins. Both the mod and symbols archives include this file,
`LICENSE`, `COPYING.md`, and `licenses/`.

## Linked and compiled dependencies

| Component | Reviewed version or revision | Terms and attribution |
|---|---|---|
| CommonLibSSE-NG | `b93280e832f263dbef44e44cbe2936622a02f91a` | [MIT](licenses/CommonLibSSE-NG.txt), Copyright (c) 2018 Ryan-rsm-McKenzie |
| spdlog | v1.15.3, `6fa36017cfd5731d617e1a934f0e5ea9c4445b13` | [MIT](licenses/spdlog.txt), Copyright (c) 2016 Gabi Melman |
| fmt, bundled with spdlog | 11.2.0, from the spdlog revision above | [MIT with optional object-code exception](licenses/fmt.txt), Copyright (c) 2012 - present, Victor Zverovich |
| rapidcsv | v8.99, `68f57cc6c83d5e0992904398822453489d8dfac1` | [BSD-3-Clause](licenses/rapidcsv.txt), Copyright (c) 2017 Kristofer Berggren |
| nlohmann/json | v3.12.0 | [MIT](licenses/nlohmann-json.txt), Copyright (c) 2013-2025 Niels Lohmann; embedded components below |
| SKSE Menu Framework API wrapper | `1dcb70179076aae4ab626f43c5baab2735ca5877` | [LGPL-2.1](licenses/SKSE-Menu-Framework-API.txt), QTR-Modding/SKSE-Menu-Framework-3-API |

CommonLib, spdlog and their included code enter the plugin build. JSON also
enters the standalone validator. The menu wrapper is compiled into the plugin
and resolves the separately installed framework through runtime exports.

The wrapper includes generated Dear ImGui/cimgui declarations. Preserve
[Dear ImGui's MIT notice](licenses/ImGui.txt), Copyright (c) 2014-2024 Omar
Cornut, from the pinned framework tree, and [cimgui's MIT notice](licenses/cimgui.txt),
Copyright (c) 2015 Stephan Dilly. The framework's `include/cimgui.h` is byte-identical to cimgui
`1.90.8dock`, revision `7c16d31cdb9d2db3038b324fe967ffa76b02c8c4`.
Its `src/cimgui.cpp` differs only in two include paths after newline
normalization. The retained cimgui notice matches that revision byte for byte.
This identifies a matching source baseline, not the historical generator invocation.

The pinned framework README directs header users to a separate
[LGPL-2.1 API repository](https://github.com/QTR-Modding/SKSE-Menu-Framework-3-API/tree/1dcb70179076aae4ab626f43c5baab2735ca5877).
The vendored wrapper now matches that API repository byte for byte. The
separately installed framework DLL retains its own GPL terms; adopting the
API header does not change the framework license or the first-party policy.

## Components embedded in JSON

The [v3.12.0 upstream license section](https://github.com/nlohmann/json/blob/v3.12.0/README.md#license)
and vendored header identify these additional components:

- UTF-8 decoder: Copyright (c) 2008-2009 Bjoern Hoehrmann, MIT.
- Grisu2 conversion: Copyright (c) 2009 Florian Loitsch, MIT.
- Hedley: Copyright (c) 2016-2021 Evan Nemerson. The upstream README and
  REUSE metadata identify CC0; retain [CC0-1.0](licenses/CC0-1.0.txt).
  The amalgamated header also labels the section MIT.
- Portions of Google Abseil: Copyright 2018 The Abseil Authors,
  [Apache License 2.0](licenses/Apache-2.0.txt), originating in
  `absl/utility/utility.h` at `10cb35e459f5ecca5b2ff107635da0bfa41011b4`.
  The header's local SPDX label says MIT, while its provenance comment and
  the upstream README identify Apache-2.0; preserve the Apache terms too.

The MIT permission and disclaimer accompanying the MIT components above are:

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Community Shaders reference

`src/cs/BSLightingShaderMaterialPBR.h` is an unchanged reference from
Community Shaders commit `dd2677fc4020db1da91b39cebef3dbfbe8913983`.
`src/render/PBRMaterial.h` mirrors its layout for compilation. Preserve the
[COPYING](licenses/CommunityShaders-COPYING.txt) and
[EXCEPTIONS.md](licenses/CommunityShaders-EXCEPTIONS.md) texts together.
The [pinned upstream README](https://github.com/community-shaders/skyrim-community-shaders/blob/dd2677fc4020db1da91b39cebef3dbfbe8913983/README.md#license)
identifies GPL-3.0-or-later with its Modding Exception and GPL-3.0 Linking
Exception (with Corresponding Source). It identifies Skyrim variants and
hardware drivers/proprietary SDKs as Modded Code, and SKSE/CommonLib as
Modding Libraries. These upstream permissions do not grant an exception for
all first-party BetterEnchantmentEffects code.

## Assets and release scope

The presenter DDS textures are generated by `tools/presenter-textures.py` as
one-pixel black textures. The explicit candidate inventory contains no game
texture files, peer-plugin DLLs, fonts, or Community Shaders logos. Peer
plugins and the Microsoft runtime are separately installed dependencies;
their installations are not redistributed by this package.

The current EFSH importer fixtures are authored synthetic records and generated
recipe goldens; [fixture provenance](tests/fixtures/README.md) records their
coverage and origin. Captured game records remain in Git history, which still
needs a separate publication review. Build tools and SDK inputs have their own
terms and are not included as standalone tools in these archives.

Before distribution, provide the complete corresponding source for the exact
binary candidate, including the needed dependency sources and build/install
scripts, through a reviewed GPL-compliant delivery arrangement. A dirty build
identifier or generic repository link is not evidence of matching published
source. The first-party permission is recorded in `COPYING.md`; it does not waive third-party
terms. Source/history publication and retention arrangements remain open.
