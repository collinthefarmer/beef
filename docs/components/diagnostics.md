# diagnostics/

The structured **trace** recorder and bounded warning history. It depends
only on `Core.h` and is engine-free. Every layer above it (`studio`, `render`,
`engine`, `menu`) may
include it, per its `ALLOWS` row in `tools/gate.py`, `'diagnostics':
('Core.h', 'diagnostics')`.

## What it owns

The trace turns a named `Event` and its fields into a bounded, rotated JSONL
record on disk, tagged with the operation and session it happened under. It is
the structured counterpart to the human `logger::` log; `docs/conventions.md`
(*Diagnostics → Logging*) draws the line between the two. It does not own
recipe `Diagnostic`s; those belong to `Reporter` in `recipe/Recipe.h`.

`WarningHistory` (`WarningHistory.h`/`.cpp`) suppresses repeated human-log
warnings within an owner's session. `Observe` returns first occurrence,
repeat, a one-time budget notice, or an omitted over-budget warning.
It retains at most 1024 keys and 128 KiB of key text; keys that do not fit
are not retained. An oversized key does not
prevent later smaller keys from fitting. `Inspect` exposes retained key and
byte counts; `Clear` releases the keys and resets the notice. All operations
share a mutex. The engine manager owns one history for its fault lines and
clears it on save loading/new game. The recipe editor owns a second history
for edit diagnostics. `LogOnce` (`engine/LogOnce.h`) is the one writer over
both. The original recipe/stack diagnostics remain available to the editor
even when their log entries are suppressed.

## Data

The trace types below are declared in `Trace.h`; `Trace.cpp` implements them.

### The events (`enum class Event`)

An event names one kind of state change the trace can record. A call site
picks the event and attaches its fields; every consumer — the JSONL file,
the recent ring, `tools/trace-report.py` — filters and groups by it.
`kEventNames` (a `Named<Event>` table in `Trace.cpp`) and `NameOf` give each
event its wire name.

| Event | Records | Example emitter |
|---|---|---|
| `kStartup` | The run identity: build, source state, fingerprints. Once per launch. | `main.cpp` at plugin load. |
| `kSettings` | The effective settings value and its fingerprint. | `SettingsFile.cpp` after a settings load or save. |
| `kRecipe` | One loaded **recipe**: its id and content fingerprint. | `main.cpp` at data load. |
| `kOperation` | A new operation id and its parent, with the reason. | `Trace::Operation` itself (`Trace.cpp`). |
| `kPage` | The current menu page and selection, de-duplicated. | `Trace::Page` itself (`Trace.cpp`). |
| `kQueue` | One `SessionQueue` action — `post`, `mark_stale`, `execute_sync`, `sync_rejected`, `equip_finalize` — with the actor and queue generation. | `SessionQueue.cpp`. |
| `kLoad` | A load transition — `resume`, `clear_begin`, `clear_end` — with the generation and actor count. `clear_end` also counts the images dropped from the image store. | `Manager.cpp`. |
| `kApplication` | An application step — `sync_begin`, `actor_decide`, `installed`, or a phase from `ApplicationPhaseName` — with the actor and recipe. | `ManagerApply.cpp`, `ApplicationService.cpp`. |
| `kRetire` | Retirement `begin`/`end` for one actor, with its recipe count. | `ManagerApply.cpp`. |
| `kBinding` | A **binding** install or geometry scope, with the geometry pointer and name. | `Binding.cpp`, `ManagerApply.cpp`. |
| `kRestore` | One per-**slot** restore on a binding, with the slot and material. | `Binding.cpp`. |
| `kTexture` | A pool or **presenter** action — `presenter_loaded`, `presenter_rejected`, `lease_rejected`, `destroy` — with the path or **target** id. | `RenderTargetPool.cpp`, `TextureRef.cpp`, `TextureLabLifecycle.cpp`. |
| `kShell` | A **shell** lifecycle step — `cloned`, `skin`, `bone`, `first_pose`, `detach` — with the clone pointer. | `Shell.cpp`, `SkinPalette.cpp`. |
| `kPreview` | A preview action — `render_request` — with the generation and source. | `TexturePreviews.cpp`. |
| `kCaptureFailure` | A field capture that threw inside `Safely`/`EmitSafely`. The event replaces the crash. | `Trace.cpp`. |

### One record's shape

Every emitted line is built from these three records. The `Field`s carry
the event's payload, and the ambient `Context` ties the line to the operation
and session that caused it, so a report can reconstruct one operation's
events across threads. `Status` exposes the recorder's health without
reading the file.

| Type | Description | Example |
|---|---|---|
| `Field{name, value}` | One key/value pair of an event. | `{"action", "execute_sync"}` |
| `Context{operation, session}` | The id pair every event carries. | Set by an enclosing `Scope`. |
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

### Operation tagging

Every event carries the operation and session it happened under; these
symbols establish that context. A boundary — a menu command, a queue task —
opens a `Scope`, and everything emitted on that thread inside it carries
the same operation id, so a report can group one gesture's events.

| Symbol | Description | Example |
|---|---|---|
| `Scope` | The RAII guard that sets the thread-local current `Context` for its lifetime. | `const Trace::Scope scope{context};` |
| `Current` | Reads the thread-local `Context`. | Called by `Emit`. |
| `Operation(reason)` | Opens a new operation id under the current one and emits a `kOperation` event that records its parent. | `Trace::Operation("reload recipes")` |
| `NextID`, `BeginSession` | Mint the operation and session ids. | Called at startup and per operation. |

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

### Counters (`Metrics.h`)

`Metrics.h` declares process-wide performance counters in namespace
`Metrics`; `Metrics.cpp` keeps them as relaxed atomics. Each `Count*`
function adds to one counter or raises one peak. `Drain` returns a
`Snapshot` and resets the per-interval counters. `Drain` does not reset the
live target counts `targets`, `targetBytes` and their peaks.

| Symbol | Description |
|---|---|
| `Section` | One timed section of a frame: `kFrame`, `kTick`, `kSnapshot`, `kSignals`, `kRender`, `kLights`, `kPreviews`, `kSweep`, `kRenderInputs`, `kRenderSlots`, `kRenderShell`, `kRenderEvaluate`, `kRenderExecute`, `kCull`. `kSections` gives each its wire name. |
| `SectionTime{micros, maxMicros}` | The total and the largest single time of one section. |
| `Snapshot` | One drained interval: sync, hidden-draw, sink, readback, render target, step and section counters, spike and hitch counts, and actors in and out of view. `Of` returns one section's `SectionTime`. `Quiet` is true when the interval has no syncs, sink changes, readbacks or render evaluations. |
| `StepKindCounts` | One counter per render step kind, `kStepKindSlots` (32) wide. |
| `kPluginSpikeMicros` | 8, 16 and 33 ms. `CountSection` counts a `kFrame` time above each threshold as a plugin spike. |
| `kHitchMicros` | 50 and 100 ms. `CountFrameAt` counts a frame interval above each threshold as a hitch. |
| `MippedRgbaBytes(side)` | The byte estimate side × side × 4 × 4 / 3 for a square four-byte-per-texel texture with mips. |
| `Stopwatch` | Starts at construction; `Micros` returns the elapsed microseconds. |

### GPU span timing (`GpuTiming.h`)

`GpuTiming.h` declares the engine-free bookkeeping for GPU timestamp
queries; `GpuTiming.cpp` implements it. `render/TextureLab.h` owns one
`Ring` and one `Totals` and issues the queries. A tick records named spans
into a free slot. A later `Resolve` turns the slot's timestamps into
per-key totals and frees the slot.

| Symbol | Description |
|---|---|
| `kFrameSlots`, `kMaxSpans` | 8 slots in the ring; at most 2048 spans in one slot. |
| `SlotState` | `kFree`, `kRecording` or `kPending`. |
| `FrameSlot{state, spans}` | One tick's state and its span keys in order. |
| `Ring{slots, order, recording, nextTick}` | The slots, the tick number that started each slot, and the slot that records now. |
| `SpanTotal{count, nanoseconds, maxNanoseconds}` | The accumulated time of one span key. |
| `Totals` | The `SpanTotal` per key, and counts of timed, dropped and discarded ticks and of untimed spans. |
| `Stamps{begin, end}`, `ResolvedTick{disjoint, frequency, spans}` | The query results for one slot. |
| `BeginTick` | Claims a free slot. With no free slot it counts a dropped tick and returns nothing. |
| `OpenSpan` | Adds a span key to the recording slot. Past `kMaxSpans` it counts an untimed span and returns nothing. |
| `EndTick` | Marks the recording slot pending. |
| `PendingOldestFirst` | Lists pending slots in the order their ticks began. |
| `Resolve` | Adds the slot's spans to `Totals`, or counts a discarded tick when the results are disjoint, have zero frequency or have the wrong span count. It frees the slot. |
| `Drain` | Returns the totals and resets them. |

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
  │  session, operation, event name (NameOf), fields, truncated, dropped
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
| `Metrics.h`/`.cpp` | `Section`, `Snapshot`, the `Count*` functions, `Drain`, `MippedRgbaBytes` and `Stopwatch`. |
| `GpuTiming.h`/`.cpp` | The GPU timestamp slot ring and per-span totals: `Ring`, `Totals`, `BeginTick`, `OpenSpan`, `EndTick`, `Resolve`, `Drain`. |
| `WarningHistory.h`/`.cpp` | Mutex-protected, bounded warning deduplication with explicit owner-controlled session reset and a one-time budget notice. |

## See also

- `docs/conventions.md` → *Diagnostics → Logging* — the line between
  `Trace::Emit` (structured trace) and `logger::` (human log).
- `REFERENCE.md` → *Foundation* — the rotation sizing, the thread-local
  context contract, and why the mutex is diagnostic-only, not a renderer
  synchronization mechanism.
