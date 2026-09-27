# Third-party notices

BetterEnchantmentEffects is GPL-3.0-only with the additional permission in
[COPYING.md](COPYING.md). The components below keep their own terms. Their
license texts are in `licenses/`.

| Component | Version | License | Copyright |
|---|---|---|---|
| CommonLibSSE-NG | `b93280e832f263dbef44e44cbe2936622a02f91a` | [MIT](licenses/CommonLibSSE-NG.txt) | (c) 2018 Ryan-rsm-McKenzie |
| spdlog | v1.15.3 | [MIT](licenses/spdlog.txt) | (c) 2016 Gabi Melman |
| fmt, bundled with spdlog | 11.2.0 | [MIT with object-code exception](licenses/fmt.txt) | (c) 2012 - present, Victor Zverovich |
| rapidcsv | v8.99 | [BSD-3-Clause](licenses/rapidcsv.txt) | (c) 2017 Kristofer Berggren |
| nlohmann/json | v3.12.0 | [MIT](licenses/nlohmann-json.txt) | (c) 2013-2025 Niels Lohmann |
| json: UTF-8 decoder | | [MIT](licenses/nlohmann-json.txt) | (c) 2008-2009 Bjoern Hoehrmann |
| json: Grisu2 conversion | | [MIT](licenses/nlohmann-json.txt) | (c) 2009 Florian Loitsch |
| json: Hedley | | CC0-1.0 | (c) 2016-2021 Evan Nemerson |
| json: portions of Google Abseil | | [Apache-2.0](licenses/Apache-2.0.txt) | (c) 2018 The Abseil Authors |
| SKSE Menu Framework API wrapper | `1dcb70179076aae4ab626f43c5baab2735ca5877` | [LGPL-2.1](licenses/SKSE-Menu-Framework-API.txt) | QTR-Modding |
| Dear ImGui declarations, in the wrapper | | [MIT](licenses/ImGui.txt) | (c) 2014-2024 Omar Cornut |
| cimgui declarations, in the wrapper | 1.90.8dock | [MIT](licenses/cimgui.txt) | (c) 2015 Stephan Dilly |
| Community Shaders material layout | `dd2677fc4020db1da91b39cebef3dbfbe8913983` | [GPL-3.0-or-later](licenses/CommunityShaders-COPYING.txt) with [exceptions](licenses/CommunityShaders-EXCEPTIONS.md) | Community Shaders contributors |

`src/render/PBRMaterial.h` is modified from Community Shaders'
`BSLightingShaderMaterialPBR.h`. It mirrors that header's data layout.
`src/cs/` and `src/extern/` hold unchanged upstream copies; their `SOURCE.txt`
files record where each copy came from.

SKSE, SKSE Menu Framework, Community Shaders, and the Microsoft runtimes are
installed separately and are not included in this package.
