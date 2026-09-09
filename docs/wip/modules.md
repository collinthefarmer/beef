# The modules, in plain terms

Written against the structure in `structure.md`, not the tree as it
stands. One idea per module, one sentence, and a contract where the
module is a boundary or carries a promise.

## Where untrusted input enters

Seven places. Everything else takes an already-checked value and never
re-checks it.

| Boundary | Takes | Promises |
| --- | --- | --- |
| `recipe/RecipeRead` | any bytes claiming to be a recipe | a Recipe, or complaints each naming the row at fault; never throws, never half-builds |
| `recipe/Expression` | a formula an author typed | bounded nesting, op count and stack; a bad formula is an error, never a crash |
| `recipe/Importer` | one of the game's own enchantment shaders | a recipe that reads back identical |
| `mesh/Mesh` | raw vertex bytes from the game | a mesh or nothing; never reads past the buffer |
| `engine/EngineForms` | a name that may match nothing | the record, or nothing; never a guess |
| `studio/FieldCheck` | text someone is typing | why it would be refused, before they commit it |
| `Settings` | the ini file | every field in range; an unreadable line becomes a default and a log line |

## Root

- **Identity** spells the plugin's name once and derives every path from it.
- **Core** holds the three shapes a value can have and one safe way to ask which.
- **Settings** is the player's preferences and their text form.
- **SettingsFile** is where that file lives and the process's copy of it.

## recipe/ — what an effect is

- **Recipe** is the records an effect is made of, and the rules for what is legal where.
- **Words** is every word the format uses, one row each, complete by compiler assertion.
- **RecipeRead** turns a file into a Recipe.
- **RecipeWrite** turns a Recipe back into a file.
- **Expression** is the small arithmetic language an author writes formulas in.
- **Signals** works out the order an effect's values must be computed in, then computes them once a frame.
- **Merge** decides who wins when two effects claim the same item.
- **Importer** converts a vanilla enchantment shader into a recipe.

## mesh/ — what an effect is applied to

- **Mesh** turns the game's packed bytes into triangles, and triangles into a flat map.
- **Islands** finds a mesh's separate pieces and texture patches, and names them.
- **MaterialClusters** groups a surface into a few kinds and names them.
- **MeshFacts** is the plain facts about a worn piece: body slots covered, bones that move it.
- **TextureSize** is the one number a working texture may be.

## engine/ — how an effect reaches an actor

- **Environment** answers questions about the person wearing the item.
- **RecipeStore** owns the recipes read from disk.
- **MeshReader** fetches a worn item's mesh and remembers it until it changes.
- **ActorState** is the record of what this plugin has done to whom.
- **ActorApply** puts an effect on.
- **ActorTick** advances every active effect one frame.
- **RecipeCommands** runs the editor's requests at a safe moment.
- **SnapshotBuild** copies the current state into a flat picture the editor may read.
- **Manager** is the queue everything goes through.
- **Events** and **Hooks** turn the game's callbacks into ours.

## render/ — how an effect becomes pixels

- **PBRMaterial** mirrors Community Shaders' material layout byte for byte, asserted at compile time.
- **RuntimeTextures** owns the graphics resources and runs the passes.
- **Compositor** draws one output's stack of layers for one piece of geometry.
- **Binding** is the only thing that writes to the game's own objects. It saves everything it overwrites and restores it, and refuses to restore anything another mod has since taken.

## studio/ — the editor's model, with no game in it

- **Snapshot** is everything the editor may look at, as plain rows.
- **View** is what is shown and how time runs.
- **Selection** is what is picked and what that resolves to.
- **Rows** turns a record into a row.
- **Panels** is what each panel contains.
- **Names** handles naming and matching.
- **Forms** describes an editable field as data: what it shows, what it accepts, what change committing it makes.
- **Edits** applies one named change to a recipe, or refuses it with a reason and changes nothing.
- **History** is undo and redo.
- **Intent** is the vocabulary of things a user can ask for.
- **MenuState** is the page's transient state and the single place an intent becomes a change.
- **Mask**, **MaskPresets**, **TermTemplates**, **MaskSession** are the mask editor: building a mask from a stack of terms, the shipped starting points, the catalogue to add from, and one editing session.

## menu/ — the editor's surface

- **Menu** registers the pages.
- **MenuWidgets** holds every drawing mechanic in one place.
- **Intents** collects what the user asked for.
- **FormDraw** draws a field.
- The rest are one page each.

## The three promises everything else rests on

`Binding` restores everything it touched or refuses to. `PBRMaterial`
matches Community Shaders' layout exactly. `Expression` is bounded in
three dimensions at once. If any of those is wrong the plugin crashes
someone's game; nothing else in the tree can.

## Relation to the documentation method

This is Stage 3 of `method.md` — the ordered vocabulary — arrived at from
the code rather than from the reader. The two should be reconciled when
the writing starts: a term here that a recipe author never meets belongs
in the developer's guide, not the author's.
