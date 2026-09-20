# diagnostics/

The structured **trace** recorder. It depends only on `Core.h` and is
engine-free. Every layer above it (`studio`, `render`, `engine`, `menu`) may
include it, per the `tools/layers.sh` row `[diagnostics]="Core.h
diagnostics"`.

## What it owns

One job: turn a named `Event` and its fields into a bounded, rotated JSONL
record on disk, tagged with the command and session it happened under. It is
the structured counterpart to the human `logger::` log; `docs/conventions.md`
(*Diagnostics → Logging*) draws the line between the two. It does not own
recipe `Diagnostic`s; those belong to `Reporter` in `recipe/Recipe.h`.

## Data

All types are declared in `Trace.h`; `Trace.cpp` implements them.

### The events (`enum class Event`)

An event names one kind of state change the trace can record. A call site
picks the event and attaches its fields; every consumer — the JSONL file,
the recent ring, `tools/trace-report.py` — filters and groups by it.
`kEventNames` (a `Named<Event>` table in `Trace.cpp`) and `NameOf` give each
event its wire name.

| Event | Records | Example emitter |
|---|---|---|
| `kStartup` | The run identity: build, source SHA, fingerprints. Once per launch. | `main.cpp` at plugin load. |
| `kSettings` | The effective settings value and its fingerprint. | `SettingsFile.cpp` after a settings load or save. |
| `kRecipe` | One loaded **recipe**: its id and content fingerprint. | `main.cpp` at data load. |
| `kCommand` | A new command id and its parent, with the reason. | `Trace::Command` itself (`Trace.cpp`). |
| `kPage` | The current menu page and selection, de-duplicated. | `Trace::Page` itself (`Trace.cpp`). |
| `kQueue` | One `SessionQueue` action — `post`, `execute_refresh`, `refresh_rejected`, `equip_finalize` — with the actor and queue generation. | `SessionQueue.cpp`. |
| `kLoad` | A load transition — `resume`, `clear_begin`, `clear_end` — with the generation and actor count. | `Manager.cpp`. |
| `kApplication` | An application step — `refresh_begin`, `installed`, or a phase from `ApplicationPhaseName` — with the actor and recipe. | `ManagerApply.cpp`, `ApplicationService.cpp`. |
| `kRetire` | Retirement `begin`/`end` for one actor, with its recipe count. | `ManagerApply.cpp`. |
| `kBinding` | A **binding** install or geometry scope, with the geometry pointer and name. | `Binding.cpp`, `ManagerApply.cpp`. |
| `kRestore` | One per-**slot** restore on a binding, with the slot and material. | `Binding.cpp`. |
| `kTexture` | A pool or **presenter** action — `presenter_loaded`, `presenter_rejected`, `lease_rejected`, `destroy` — with the path or **target** id. | `RenderTargetPool.cpp`, `TextureRef.cpp`, `TextureLabLifecycle.cpp`. |
| `kShell` | A **shell** lifecycle step — `cloned`, `skin`, `bone`, `first_pose`, `detach` — with the clone pointer. | `Shell.cpp`, `SkinPalette.cpp`. |
| `kPreview` | A preview action — `render_request` — with the generation and source. | `TexturePreviews.cpp`. |
| `kCaptureFailure` | A field capture that threw inside `Safely`/`EmitSafely`. The event replaces the crash. | `Trace.cpp`. |

### One record's shape

Every emitted line is built from these three records. The `Field`s carry
the event's payload, and the ambient `Context` ties the line to the command
and session that caused it, so a report can reconstruct one command's
events across threads. `Status` exposes the recorder's health without
reading the file.

| Type | Description | Example |
|---|---|---|
| `Field{name, value}` | One key/value pair of an event. | `{"action", "execute_refresh"}` |
| `Context{command, session}` | The id pair every event carries. | Set by an enclosing `Scope`. |
| `Status{events, dropped, bytes, segment, rotations, enabled, fileFailed}` | The recorder's health snapshot. | Returned by `Recorder::Inspect`. |

### Constants

These two constants bound the trace's disk footprint. Rotation keeps the
newest `kTraceSegmentsKept` segments of `kTraceSegmentBytes` each, so a
long session always has its last 32 MiB on disk and never holds more than
64 MiB.

| Constant | Value | Purpose |
|---|---|---|
| `kTraceSegmentBytes` | 32 MiB | The size of one rotated file segment. |
| `kTraceSegmentsKept` | 2 | The number of segments kept on disk. |

### The sink (`Recorder`)

`Recorder` is the one owner of the trace file and its bounds; the process
has one, reached through `Get()`. Call sites do not use it directly — they
go through the call-site API below — and `Inspect`/`Recent` expose its
state for tests and tooling.

| Member | Description |
|---|---|
| `Open`, `Enable` | Open the trace file; turn recording on. |
| `Record` | Serialize one event line and write it. |
| `Inspect` | Return the `Status` snapshot. |
| `Recent` | Read back the 512-line `recent_` ring. |
| `SegmentPath`, `OpenSegment`, `Rotate` | The rotation internals: name, open, and roll the segments. |

### Command tagging

Every event carries the command and session it happened under; these
symbols establish that context. A boundary — a menu command, a queue task —
opens a `Scope`, and everything emitted on that thread inside it carries
the same command id, so a report can group one gesture's events.

| Symbol | Description | Example |
|---|---|---|
| `Scope` | The RAII guard that sets the thread-local current `Context` for its lifetime. | `const Trace::Scope scope{context};` |
| `Current` | Reads the thread-local `Context`. | Called by `Emit`. |
| `Command(reason)` | Opens a new command id under the current one and emits a `kCommand` event that records its parent. | `Trace::Command("reload recipes")` |
| `NextID`, `BeginSession` | Mint the command and session ids. | Called at startup and per command. |

### Call-site API

These free functions are what the rest of the codebase calls; no module
constructs a `Recorder` or a `Context` itself. Use `EmitSafely` or `Safely`
whenever computing the fields can throw, so recording a diagnostic can
never become a crash.

| Function | Description | Example |
|---|---|---|
| `Get()` | Returns the process `Recorder`. | `Trace::Get().Inspect()` |
| `Emit(Event, fields)` | Records one event against `Current()`. | `Trace::Emit(Event::kLoad, {{"action", "resume"}})` |
| `EmitSafely(Event, fields)`, `Safely(capture)` | Guard a capture that can throw. The exception becomes a `kCaptureFailure` event, not a crash. | `Trace::EmitSafely(Event::kQueue, {{"action", "post"}})` |
| `Page(page, selection)` | De-duplicates repeated page/selection pairs, then emits `kPage`. | Called by the menu each frame. |
| `Pointer(p)` | Formats an address for a field value. | `Trace::Pointer(clone_.get())` |
| `Fingerprint(text)` | Hashes an `std::string_view` for a field value. | `Trace::Fingerprint(settings.Serialize())` |

## How an event flows

```
call site                                                  anywhere above diagnostics/
  │  Trace::Emit(Event::kX, {Field{...}, ...})              Trace.cpp
  │    (or EmitSafely / Safely{...} if the capture can throw)
  ▼
Trace::Current()          reads the thread_local Context     Trace.cpp
  │                       set by an enclosing Trace::Scope
  ▼
Recorder::Record(event, context, fields)                     Trace.cpp
  │  caps fields at 64 entries / 2048 bytes each, marks `truncated`
  │  builds one nlohmann::json line: schema, run, seq, unix_ms, thread,
  │  session, command, event name (NameOf), fields, truncated, dropped
  │  pushes the line onto the 512-line `recent_` ring (read back by `Recorder::Recent`)
  ▼
Recorder::Write(line)
  │  if appending would exceed kTraceSegmentBytes ──▶ Recorder::Rotate()
  │                                                      closes the segment,
  │                                                      opens the next
  │                                                      (SegmentPath),
  │                                                      deletes the one
  │                                                      before the previous,
  │                                                      writes a "rotated"
  │                                                      event carrying the
  │                                                      kStartup identity
  ▼
<name>-trace-<run>.jsonl / -<n>.jsonl on disk (last kTraceSegmentsKept segments)
  │
  ▼
tools/trace-report.py            segments_of() finds a segment's siblings by
                                  name, lines_in_order() reads them oldest first
```

## The files

| File | What it owns |
|---|---|
| `Trace.h` | `Event`, `Field`, `Context`, `Status`, `Recorder`, `Scope`, and the free-function API: `Get`, `Current`, `Command`, `NextID`, `BeginSession`, `Emit`, `EmitSafely`, `Safely`, `Page`, `Pointer`, `Fingerprint`. |
| `Trace.cpp` | The event-name table, `Recorder`'s file and rotation logic, and the free functions. |

## See also

- `docs/conventions.md` → *Diagnostics → Logging* — the line between
  `Trace::Emit` (structured trace) and `logger::` (human log).
- `REFERENCE.md` → *Foundation* — the rotation sizing, the thread-local
  context contract, and why the mutex is diagnostic-only, not a renderer
  synchronization mechanism.
