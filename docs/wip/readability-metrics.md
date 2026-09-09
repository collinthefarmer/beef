# Readability metrics

Mechanical checks over the source, ordered by how much each predicts a
hard read. All run in a shell over `src/`, excluding `src/extern/`.

## Shape

**Nesting depth.** Maximum leading-tab depth inside each function body.
Deep nesting costs more than length, because each line's meaning depends
on conditions written above it. Past four levels, look.

**Function length.** Lines between matching braces at namespace scope.
Report the maximum and the 95th percentile per file. A mean hides one
400-line function among fifty short ones.

**Parameter counts.** Functions over four parameters, and any function
taking more than one boolean.

## Domain language

**Mechanical C++ per module.** Occurrences of `std::visit`, `std::get`,
`holds_alternative`, `static_cast`, `reinterpret_cast` and `template<`
per hundred lines, by file. The second rule in `CLAUDE.md` puts these
behind named helpers in one place each, so a high count marks a module
where a reader meets C++ before the domain.

**Bare literals at call sites.** `true`, `false` and `nullptr` in
argument position, counted per file. Each one sends the reader to the
signature to learn what it means.

**Vocabulary drift.** For each glossary concept, the set of spellings the
code uses for it. Two names for one thing read as two things, and this
bounds whether the documentation can describe the code in the code's own
words.

It has produced a false positive already, and the failure mode is worth
knowing before trusting it. Its top result was `src/ComposePage.cpp`
naming one thing three ways, geometry 121 times, piece 98 and item 20.
`PieceRow` and `GeometryRow` are two different domain records — a piece
is an actor plus an armor plus first or third person, a geometry is one
mesh shape within that piece — and every one of the twenty "item" hits is
ImGui's own vocabulary: `NextItemWidth`, `ItemSpacingX`, `IsItemActive`,
`BeginTabItem`. The metric matched a library's API and guessed that two
real types were one concept. It needs the ordered glossary from Stage 3
of `method.md` and an exclusion for third-party vocabulary before any of
its output should drive work.

**Glossary coverage.** The fraction of identifiers in the engine-free
modules that are a glossary term, an ordinary English word, or a standard
library name. The remainder is private jargon, and its size is the entry
cost of the codebase.

**Abbreviation inventory.** Identifiers under four characters or without
vowels, excluding loop counters. `efsh`, `av` and `rmaos` may each be
justified; the list of them is the list a glossary must define.

## Conformance, expected to read zero

Comments outside `src/extern/`, `auto` in a signature, raw `new` and
`delete`. A nonzero result finds drift from a rule the repo already
holds, rather than a judgement call.

## Tied to the writing

**Citation spread per question.** Group the fact base's citations by the
question they answer. One file behind a question means the code carries
the answer; six means it does not.

**One-sentence responsibility.** Writing a documented answer against a
module and finding no single sentence that states what the module is
responsible for is the test for a confused module.
