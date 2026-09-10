# Convention cleanup

The pure core mostly follows docs/conventions.md; these spots do not yet.
Clear them before wave 2 builds on the core. Delete an item when done.

1. **Diagnostics pushed directly in `Signals.cpp`.** `CheckCurve`, `CheckMask`,
   `CheckUniqueNames`, `CheckVariants`, and `CheckSlotExclusions` build
   `{Severity::kError/kWarning, where, msg}` and `push_back` instead of going
   through a `Reporter`, while their siblings `CheckSource`/`CheckLayer` use
   `Reporter report{out, where}`. `CheckUniqueNames` even defines local
   `error`/`warn` lambdas that duplicate `Reporter::Error`/`Warn`. Route them
   through `Reporter` so wave-2 code has one example to copy.

2. **`Importer.cpp` is a second JSON dialect.** It uses `nlohmann::json`
   (unordered, not the `ordered_json` the rest of recipe/ uses), hand-rolls
   `FloatAt`/`TextAt`/`ColorFrom` over raw `.contains()`/`.at()`/`.get()`, and
   returns `std::expected<…, std::string>` (a single error) rather than the
   `LoadResult`/`Reporter` diagnostics model. It parses a decompiled effect-
   shader dump rather than a recipe, which partly justifies not using the
   recipe `Reader`; but the raw-json field access is exactly the dialect split
   clusters.md flags. If a small typed reader over the dump is worth it, base
   it on `Reader`/`Reporter` so there is one JSON vocabulary.

3. **Duplicated `GlobMatch`.** `Resolve.cpp` exports `GlobMatch` and
   `Signals.cpp` has a private `GlobMatches` with an identical body (used by
   trigger/event matching). One should call the other so the glob semantics
   live in a single place.
