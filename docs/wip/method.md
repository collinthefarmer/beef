# How these documents get written

An experiment in producing documentation that a stranger can read. The
method separates collecting facts from writing prose, and measures
readability with readers who genuinely lack context rather than with the
writer's guess about what a reader lacks.

## Stage 1 — Interfaces, then questions

Read only what a stranger meets: the installed file tree, the recipe
schema and its example, the menu's labels and controls, the log lines,
`src/Identity.h`. Read shapes and strings, never sentences.

`README.md`, `ARCHITECTURE.md`, `REFERENCE.md`, `NOTES.md` and the plans
stay closed. Prose the project wrote about itself sets the writer's
framing before the audience model exists, and it may have rotted.

Output: `questions.md` — the reader model and a flat list of questions in
the readers' own words. The question list drives everything after it.
Without it a code read produces a tour of the file tree, which is
accurate and unreadable.

## Stage 2 — A fact base, not a draft

Read the code. Produce atomic claims, one per line, each carrying a
citation: `src/Recipe.cpp:214`. No prose, no ordering, no audience.

A claim that cannot carry a code citation carries one of two other marks.
`[spec]` means an external reference states it. `[verify]` means the
writer believes it but has not confirmed it. Every `[verify]` is resolved
by building, running a test, or reading a log before it reaches a
document. Unresolved claims get cut, never softened into a hedge.

The project's own prose is a source of questions and candidate terms
here, never a source of facts. Every claim it makes is re-derived from
code or dropped.

## Stage 3 — Order the vocabulary

Extract every domain term the fact base uses and put the terms in
dependency order: each one defined using only terms already defined, plus
plain English. A cycle in the ordering means the model itself is unclear,
so the cycle gets resolved before anything is written.

The ordering gives a mechanical check on every draft. Every domain term
in the prose appears in the glossary, and no term appears before the
section that defines it.

## Stage 4 — Write against the plan

Each section answers one question from Stage 1, using only claims from
the fact base. A sentence that traces to no claim does not go in.

Three rules hold the line between instructive and overbearing:

- Every procedure gives the command and the observable result that means
  it worked. No procedure ends without telling the reader how they know
  it succeeded.
- Explain a constraint where the reader could otherwise choose wrong.
  Skip it everywhere else.
- Delete any sentence whose only job is to say that something matters.

Plain technical English and active voice throughout. Put the actor in the
subject and the action in the verb.

## Stage 5 — Two checks, both from fresh contexts

The checks need different inputs, so they cannot be one pass.

**Comprehension.** A reader holding the draft and no repo access answers
a fixed set:

1. What does this do, in one sentence?
2. Who installs it, and what changes for them?
3. List every term you could not define from this document.
4. Name a step you could not perform.
5. What did you have to assume?

The count of unresolved terms is the number to drive down between drafts.

**Accuracy.** A reader holding repo access and the citations verifies
each claim against the code, then hunts for prose that no citation
supports.

## Feedback loop

Every comprehension failure resolves to exactly one of three causes: a
gap in the fact base, a gap in the term order, or a writing failure.
Classifying each failure keeps the loop from turning into polishing.

## The old documents come last

Once a draft passes both checks, diff it against `README.md`,
`ARCHITECTURE.md`, `REFERENCE.md` and `NOTES.md` to find topics they
cover that the draft missed. By then they work as a checklist and their
framing can no longer reach the writing.

## Two kinds of question, from a trial run

A trial answered three questions with small fast models: one player
troubleshooting question, one on how recipes match items, one on what a
shell is. The two conceptual answers came back clean. The troubleshooting
answer carried three wrong details.

The split follows the shape of the content, not the model.

**Enumerable questions** have answers that already exist as a structure
in the code: the seven key kinds and their priorities in
`src/Vocabulary.h`, the signal kinds, source kinds, output slots, blend
modes, material channels, waveforms and partitions in
`schema/recipe.schema.json`. The answer is a name and one line per
member of a closed set. Transcription, and it goes well.

The trial's key list is the model to follow: one bold name, one sentence
of what it matches, and the closed set of priorities stated beside it as
numbers.

**Assembled questions** have answers that exist nowhere in the code. A
troubleshooting procedure has to gather log lines from five files, put
the checks in the order a reader performs them, and work out what a
player does about each. This is where the trial put its errors, and it
is the work that needs the accuracy check.

## Rules the trial added

- **Quote strings verbatim.** Any string a reader will type, search for,
  or see on screen is copied character for character from the source. The
  trial cited a real line at `src/RecipeStore.cpp:367` and quoted
  `recipes: X loaded` for a line that reads `recipes: {} loaded, {} with
  errors, {} unresolved editor IDs, {} imported this session, folder {}`.
  A reader searching for the quoted text finds nothing. The accuracy
  check compares quoted text against the source, and does not stop at
  confirming the cited line exists.
- **A `[verify]` claim leaves the prose.** The trial stated a log path in
  the prose and marked the same path uncertain underneath. The mark has
  to remove the claim, or it records doubt where no reader sees it.
- **End at the last cited fact.** Both conceptual answers were accurate
  until a closing paragraph that reasoned about why an author would
  choose one option, unsupported by anything read.
- **Fence the question, not the word count.** All three answers ran two
  to three times longer than their question needed, and the excess was
  the neighbouring questions. The shell answer added a field reference
  belonging to question 18 and a blend explanation belonging to question
  22; cut to what question 19 asked, it fell from 262 words to 81 with
  nothing accurate lost. The keys answer took on question 15. The player
  answer took on questions 2 and 3, then explained recipe keys to a
  reader who never opens a recipe. Every dispatch names the neighbouring
  questions by number and says to refer to them rather than answer them.
- **Open with the first fact.** The keys answer spent three clauses
  restating that keys decide matching before it said anything. A ban on
  restating the question does not catch a restatement written as a
  definition.
- **Fence the answer.** All three answers opened with the writer's own
  working narration despite a rule against it. Emit the answer alone,
  everything else after a separator.

## Working backwards: the code as the explanation

`src/Vocabulary.h` is why the key list came out clean. Seven rows, each
holding the enum, the JSON name and the priority together, with a
`static_assert` that the table stays complete. A reader of that table
cannot get the set wrong or miss a member.

Stage 2 measures which answers lack such a table, at no extra cost: group
the fact base's citations by question. One file behind a question means
the code carries the answer. Six files mean it does not, and that
document will stay fragile however well it is written. The player
troubleshooting question drew fourteen citations across seven files, and
it is the answer that broke.

Comments are forbidden here, so the levers are types, names, tables and
single-sourcing.

- **Single-source the closed sets.** The seven key priorities exist as
  numbers in `kKeyKinds` at `src/Vocabulary.h:10` and as English inside a
  `description` string at `schema/recipe.schema.json:18`. Nothing keeps
  the two copies equal. Generating the schema's enumerations from the
  vocabulary tables leaves one source to cite and one place to change.
- **Give the skip reasons a type.** About 126 `logger` calls sit across
  eight files, and every condition under which the plugin does nothing is
  an ad-hoc call at its own site. A closed enum of reasons, each variant
  carrying its message, makes the set exhaustive by construction instead
  of by grep, and turns the hardest question in the list into an
  enumerable one.
- **Name what a reader sees.** Format strings retyped from call sites are
  what produced the mis-quoted log line. The menu shows board, offers,
  terms and pin; where no type carries a name the menu says, either the
  code should carry it or the menu should stop saying it.

Some assembly is legitimate. A question with scattered citations is a
code problem only when the scattering has no reason behind it. Stage 2
therefore produces two things: the fact base, and a short list of places
where the code could hold an answer it currently makes a reader assemble.
