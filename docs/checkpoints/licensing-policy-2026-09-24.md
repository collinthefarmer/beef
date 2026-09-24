# First-party licensing policy adoption

The owner confirmed sole copyright ownership of original project code and
accepted GPL or more permissive licensing. Following review of the concrete
draft, the project adopts **GPL-3.0-only with the additional permission in
[COPYING.md](../../COPYING.md)**. The GPL text remains unchanged.

## Scope review

- The grant covers rights controlled in original project material. It permits
  the named Skyrim/SKSE/CommonLib, Community Shaders, menu framework and required
  Windows/graphics combination, without granting rights in those components.
  It preserves GPL source obligations and upstream attribution, source and
  relinking requirements. Its component list is independent of compatibility
  version pins. Future original contributions must carry the same permission;
  downstream removal remains allowed under GPL section 7.
- The pinned framework README directs API consumers to the separate LGPL API
  repository. The adopted header is from that repository, not the GPL
  framework-tree copy. Its inline code, full source, license and a tested
  rebuild route are included. We do not rely on every inline function meeting
  LGPL's small-header exception. The framework DLL remains separately supplied;
  its terms are not changed or waived by the first-party grant.
- The Community Shaders reference and adapted portions of `PBRMaterial.h`
  retain the upstream GPL terms and modding/linking exceptions. The material
  header now explicitly points to those terms alongside its original portions'
  first-party notice. No vendored file or upstream license was edited.
- MIT/BSD dependencies and JSON's embedded notices remain unchanged. Nothing
  in the new permission authorizes redistribution of proprietary game or SDK
  material. No claim is made that a first-party grant resolves every possible
  third-party runtime licensing question.

References: [GPL section 7](https://www.gnu.org/licenses/gpl-3.0.html#section7),
[FSF linking guidance](https://www.gnu.org/licenses/gpl-faq.en.html#GPLIncompatibleLibs),
[LGPL 2.1 sections 5–6](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.en.html),
the [pinned framework README](https://github.com/QTR-Modding/SKSE-Menu-Framework-3/blob/c97cdce6dd207c7cf1401611bc82bd7e8f97a814/README.md),
and the [retained Community Shaders exceptions](../../licenses/CommunityShaders-EXCEPTIONS.md).

## Implementation and validation

- Added short legal references to 356 first-party C++ files and the generated
  plugin declaration template. Required legal notices are explicitly excepted
  from the coding convention against explanatory comments.
- `COPYING.md` is included in staging, mod and symbols inventories and the
  curated source inventory. It participates in build identity; an integration
  test verifies that changing it regenerates the identity.
- All 357 C++ files/templates match the previous clean-extraction snapshot
  byte for byte after removing only the newly inserted legal notices.
- License tests: 3 passed; generated-output integration: 1 passed;
  compatibility: 3 passed; source archive: 5 passed; package regression: 8 passed.
- Windows Release build and mod/symbols candidate packaging passed. The new
  source archive passed inventory/hash verification. All three archives contain
  the exact adopted `COPYING.md`; the GPL text remains byte-identical to HEAD.
- A fresh source extraction generated an identity manifest exactly matching
  the new Windows candidate.

The previous 102-test clean-extraction run remains evidence for the code before
these legal-notice-only C++ edits. This pass does not claim a repeated full
native/sanitizer run. Runtime acceptance, history publication review and actual
source/binary hosting and retention remain release work. Nothing was installed
or published.

## Candidate artifacts

Build identity: `1303a555e66b-8f005f6026e8f551-Release-ce036c51`.

- `BetterEnchantmentEffects-0.1.0-steam-1.6.1170-1303a555e66b-8f005f6026e8f551-Release-ce036c51.zip`
  SHA-256: `714c15665386a9b4af986fe926f1102b640165bffe1b56054eb150d27d757827`.
- `BetterEnchantmentEffects-0.1.0-steam-1.6.1170-1303a555e66b-8f005f6026e8f551-Release-ce036c51-symbols.zip`
  SHA-256: `72b705deeab9d3ff32d9cfe2166ec3f6684e735f6f9ec9762d0e0c474f44d640`.
- `BetterEnchantmentEffects-steam-1.6.1170-1303a555e66b-8f005f6026e8f551-Release-ce036c51-source.tar.gz`
  SHA-256: `1a5392434996a01e2f721a27780b3b160169f5dd0eca9f9122ad4d7f76bebd2f`.
