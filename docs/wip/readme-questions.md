# What the README answers

Built the same way as `questions.md`: from Stage 1 surfaces, before
reading any of the project's prose about itself.

## Who arrives here

Nobody arrives at a README committed. Every other document assumes the
reader has already decided to use the plugin. The README serves the
person still deciding, and then points them at the document that assumes
they have.

Three arrivals:

- A player following a requirement link from someone's mod page.
- An effect author asking whether this framework can build the effect
  they already have in mind.
- A developer who found the repository and wants to know what it is.

## The questions

**Is this for me?** — the first screen

1. What is this, in one sentence I could repeat to someone else?
2. What does it look like?
3. What could I not do before this existed?
4. What must my setup already have?
5. Does it ship any effects, or do I need something else as well?

**Getting it running**

6. How do I install it?
7. How do I confirm it is working?
8. It is not working. What do I check?

**What can it do?**

9. What kinds of appearance changes can it make?
10. What can an effect react to?
11. What does a recipe look like?
12. What can it not do?

**Where do I go next?**

13. I want to make an effect.
14. I want to extend the framework.
15. I want the exact recipe format.

**Project facts**

16. What state is this in, and which Skyrim and Community Shaders
    versions does it target?
17. How do I build it from source?
18. What licence, and what does that ask of me?
19. Where do I report a problem?

## What the README refuses to answer

The README answers what, whether, and where next. It answers how only
twice: how to install, and how to confirm the install worked. Every other
"how do I" goes to another document.

Against the 36 questions in `questions.md`:

- Player questions 1 to 4 are answered here in full. This is the whole
  player path, and it is why an author can link here rather than write
  their own requirements section.
- Player questions 5 to 8 get a sentence each and a pointer.
  `BetterEnchantmentEffects.ini` carries its own comments per setting, so
  the README names the file and stops.
- Author questions 9 to 36 are routed, not answered. Questions 9 and 10
  above give the capability envelope so a reader can judge whether their
  idea is buildable, without teaching them to build it.

## Order

Question 4 comes early, next to the pitch. Three conditions each make the
plugin do nothing — Community Shaders absent, geometry without PBR
textures, no recipes installed — and a reader whose setup fails one of
them should find out on the first screen rather than after being sold.

## Budget

Under 250 lines including the example. The current README runs 952 lines
across 15 headings, about 63 lines a heading. A section that will not fit
its budget is a section whose content belongs in another document; that
is the signal to move it, not to trim sentences out of it.

## Open dependencies

- **Question 2 needs images.** A visual mod whose README shows nothing
  fails at its first job. Screenshots or a short capture have to come
  from a running game, which means from the user.
- **Question 16 needs version numbers.** The supported Skyrim runtime and
  the Community Shaders version this was built against are facts to
  resolve in Stage 2, not to state from memory.
- **Question 11 needs a small example.** `schema/example-magicka.json`
  runs 94 lines across five outputs, three targets, eighteen signal rows,
  a shell block and a variant. It is a good end state and a poor first
  thing anyone reads. Stage 2 has to find or build the smallest recipe
  that produces a visible change.
