# BetterEnchantmentEffects: how the code is written

`README.md` holds the reading order — one line per document saying what
question it answers — and `docs/README.md` indexes everything under `docs/`.
Read them first; this file states only the rules the code obeys and the
practicalities of working here.

## Three rules, in priority order

1. **Memory safety: this code is never the cause of a crash.** No raw
   `new`/`delete`, no unchecked indexing, no unchecked `std::get`, no
   recursion without a depth limit, no trust in input: every file, every
   expression, every engine pointer is checked at the boundary and turned
   into a typed record or a reported error. Malformed input makes a row
   inert and a log line, never undefined behaviour. Engine pointers are
   null-checked at every use; forms are looked up, never assumed. Parsers
   and evaluators have explicit bounds (nesting depth, op count, stack
   size) and tests that feed them garbage.
2. **Readability and organisation: the reader is a developer, not a C++
   developer.** Code reads as the domain: recipes, signals, sources, masks,
   outputs. Mechanical C++ (variant visiting, JSON field access, string
   parsing, engine relocation) lives behind small named helpers in one
   place each, so a domain function is a sequence of domain steps. Each
   header states its data types first, then the functions over them.
   Plain records and free functions; a class only where state and
   behaviour must change together. Complete type signatures; no `auto` in
   a signature; no `Any`-like escape hatches. No comments, anywhere: not
   a header banner, a section rule, a member note or a trailing aside.
   The code says it through a name, a type or a small named helper. A
   fact the code cannot state (an engine layout, a CS rule, a decompile
   line, a packing, the reason for a constant) goes in `REFERENCE.md`
   under the module's heading.
3. **Performance: lean, clever where it matters, nowhere else.** Per-tick
   and per-texel paths (signal evaluation, mask interpretation, the
   binding's writes) are designed for cost and measured. Load-time paths
   (JSON, validation, resolution) are written for clarity; a recipe file
   is read once. Do not optimise a load-time path on speculation.

## Practicalities

- Engine-free modules (`Recipe`, `Expression`, `Signals`, `Importer`)
  compile natively and are unit-tested through `tests/run-native.sh`;
  engine-facing modules are thin adapters over them.
- `src/Identity.h` is the only place the plugin name is spelled.
- Errors carry a `where` naming the row (`signal glowLevel`, `output 2
  layer 0`) so the menu can show them in place.
- Do not modify `decompiled/`, `reference/` or anything under `/mnt/a/mods/`,
  with one exception: recipe files. The installer stages no recipes; when
  the user asks for a recipe, write it into the MO2 mod folder
  (`/mnt/a/mods/SkyrimSE/mods/BetterEnchantmentEffects/SKSE/Plugins/BetterEnchantmentEffects/recipes/<folder>/`),
  and keep a copy under `recipes/` in the repo only when it should be an
  example the tests read.
- Build with `./build.sh Release -j 4` (more jobs exhaust WSL's memory and
  kill the instance), install with `./install.sh`, and stop at
  each in-game checkpoint for the user to run the game; give the log lines
  to look for.
- Work inside `nix develop`; every script calls its tools from `PATH` and
  stops with a message naming the missing one if you are outside the shell.
- Rename a symbol with `tools/rename.py Old New --apply`, which drives
  clangd; omit `--apply` to list the edits. It renames
  references, not comments or docs, and prints what it left for a hand
  pass. It reads `build/clangd/compile_commands.json`;
  `tools/compile-db.sh` rewrites that after a source file is added.
