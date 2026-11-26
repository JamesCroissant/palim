# palim — Phase 1 MVP Architecture

Status: Phase 1 (Steps 1–5), Phase 1.5 (ring buffer + best-frame
selection), Phase 2 (raw V4L2 capture), Phase 3 (multithreading),
Phase 4 (Git integration), Phase 5 (V4L2 control metadata),
Phase 6 (timeline CLI), Phase 6.1 (software-side "what changed?"
diffing), and Phase 7 (GTest suite) are implemented. This document
exists to agree on the
shape of the system *before* writing code, per the project's own philosophy
of understanding each layer (camera → kernel → V4L2 → buffer → OpenCV →
processing → event → storage) rather than hiding it behind a library.

## Goal of Phase 1

Prove the smallest possible loop:

> physical state change on the desk → automatically detected → before/after
> frames saved with metadata

No Git integration, no timeline UI. Those are named explicitly in later
phases below so it's clear they're deferred, not forgotten. Ring buffer
+ best-frame selection (Phase 1.5), raw V4L2 capture (Phase 2), and
capture/processing/storage threading (Phase 3) *are* now implemented,
since each turned out to be a small, self-contained addition once the
pieces it builds on existed.

## Directory structure

```
palim/
├── CMakeLists.txt
├── README.md
├── LICENSE
├── .gitignore
├── docs/
│   └── ARCHITECTURE.md
├── src/
│   ├── main.cpp              # spawns capture/processing/storage threads + GUI loop
│   ├── timeline_main.cpp     # palim-timeline: reads commits/ and prints a summary
│   ├── camera.hpp/.cpp       # Camera: raw V4L2 (open/ioctl/mmap)
│   ├── camera_settings.hpp   # CameraSettings: exposure/gain/white-balance snapshot
│   ├── change_detector.hpp/.cpp   # ChangeDetector: frame diff → changed %
│   ├── state_machine.hpp/.cpp     # StateMachine: STABLE/CHANGING/WAIT_FOR_STABLE
│   ├── snapshot_writer.hpp/.cpp   # SnapshotWriter: before/after jpg + metadata.json
│   ├── frame_quality.hpp/.cpp     # computeSharpness(): shared by SnapshotWriter + FrameHistory
│   ├── ring_buffer.hpp            # RingBuffer<T>: generic fixed-capacity circular buffer
│   ├── frame_history.hpp/.cpp     # FrameHistory: recent-frames window + best-frame-in-range
│   ├── blocking_queue.hpp         # BlockingQueue<T>: thread-safe producer/consumer queue
│   ├── git_info.hpp/.cpp          # captureGitInfo(): git rev-parse/status via popen
│   └── commit_metadata.hpp/.cpp   # reads metadata.json back into a struct (for palim-timeline)
├── tests/                    # GTest suite, see "Testing" below
└── commits/                  # runtime output, gitignored
    └── 001/
        ├── before.jpg
        ├── after.jpg
        └── metadata.json
```

Flat `src/` on purpose — the file count still doesn't justify
`capture/`/`processing/`/`state/` subfolders. We'll split it out if it
ever actually gets unwieldy.

All of the above except `main.cpp` and `timeline_main.cpp` builds into a
static library, `palim_lib`, that both executables and the test suite
link against — see "Testing" below.

## Class responsibilities

- **Camera** — talks to a V4L2 device directly (`open` + `ioctl` + `mmap`,
  see Phase 2 below), and exposes one method: `grab() ->
  std::optional<cv::Mat>`. Nothing else. It doesn't know about change
  detection or storage, and its public interface hasn't changed since
  Phase 1 even though its entire implementation has.

- **ChangeDetector** — pure function object. Takes two `cv::Mat` frames,
  returns a `double` (percentage of pixels that changed), using
  grayscale → GaussianBlur → absdiff → threshold → count nonzero. Stateless
  across calls; the caller decides what to do with the number.

- **StateMachine** — the only place that holds "what is currently
  happening". States: `STABLE`, `CHANGING`, `WAIT_FOR_STABLE`. Given a
  changed-% reading and a timestamp each frame, it decides whether to stay,
  transition, or emit a `CommitEvent` (carrying the before-frame and
  after-frame). This is the piece from section 8 of the spec — everything
  else is a straight pipe, this is where the actual logic lives.

- **SnapshotWriter** — given a `CommitEvent` (before/after `cv::Mat` +
  metadata), creates `commits/NNN/`, writes `before.jpg`/`after.jpg` via
  `cv::imwrite`, and writes `metadata.json`.

- **RingBuffer\<T\>** — generic fixed-capacity circular buffer, written by
  hand (not `std::deque`) so the wraparound indexing is visible rather than
  hidden. Knows nothing about frames or time.

- **FrameHistory** — wraps a `RingBuffer<FrameSample>` (frame + steady_clock
  timestamp). Two jobs: keep the last few seconds of frames, and answer
  "what's the sharpest frame between these two timestamps?" via
  `bestFrameInRange`. Clones every frame it stores (see note below).

- **BlockingQueue\<T\>** — thread-safe producer/consumer queue (mutex +
  condition_variable). `push()` never blocks (drops the oldest item if
  full); `waitAndPop()` blocks until an item arrives or `shutdown()` is
  called. The only synchronization primitive Phase 3 needed.

- **main.cpp** — no longer a single loop. It spawns capture / processing
  / storage threads (see Phase 3 below) connected by two
  `BlockingQueue`s, and itself keeps only the GUI loop. Still no
  "Pipeline" class — the thread bodies keep the data flow visible rather
  than hiding it behind an abstraction.

## Data flow (as of Phase 3)

```
[capture thread]                     [processing thread]                [storage thread]
Camera::grab()
      │ cv::Mat frame, timestamp
      ├──► displayMutex-guarded          (nothing shared with
      │    displayFrame, for              other threads: detector,
      │    [main thread]'s GUI            stateMachine, frameHistory
      │                                   all live here alone)
      ▼
BlockingQueue<FrameSample>
  ::push()  ───────────────────►  ::waitAndPop()
                                          │
                                          ▼
                                   FrameHistory::push(frame, timestamp)
                                          ▼
                                   ChangeDetector::compare(prevFrame, frame)
                                          │ double changedPercent
                                          ▼
                                   StateMachine::update(changedPercent,
                                                         frame, timestamp)
                                          │ occasionally: CommitEvent
                                          ▼
                                   FrameHistory::bestFrameInRange(...)
                                          │ overwrites event.after if sharper
                                          ▼
                                   BlockingQueue<CommitEvent>
                                     ::push()  ──────────────────►  ::waitAndPop()
                                                                            │
                                                                            ▼
                                                                     SnapshotWriter::write()
                                                                            ▼
                                                    commits/NNN/{before.jpg, after.jpg, metadata.json}
```

Each thread body is still a straight synchronous pipe internally — one
frame in, one decision out — same as Phase 1. Threading only changed how
the stages are *connected*, not what happens inside each one.

**Note on `.clone()`:** `FrameHistory::push` deep-copies every frame it
stores. `cv::VideoCapture` backends can reuse an internal scratch buffer
across `read()` calls — a documented OpenCV gotcha — so keeping a frame
around past the next `grab()` needs its own memory, not just another
reference to the same buffer. The cost is real (a 1280×720 BGR frame is
~2.7MB, so a 90-frame history is roughly 250MB) and is a deliberate
correctness-over-memory tradeoff for now; worth revisiting if it matters
in practice.

## Minimum dependencies

- C++17
- CMake ≥ 3.16
- OpenCV (`core`, `imgproc`, `highgui`, `imgcodecs` — no `videoio` since
  Phase 2: Camera no longer uses `cv::VideoCapture`)
- Linux V4L2 headers (`linux/videodev2.h`, part of `linux-libc-dev` /
  shipped with the kernel headers most distros already have)
- pthreads (`std::thread`, `CMakeLists.txt` links `Threads::Threads`)
- No JSON library yet — `metadata.json` in Step 5 is small and fixed-shape
  enough to write by hand with `std::ofstream`. We can pull in
  `nlohmann/json` later if the schema grows; not worth a dependency for
  five fields.

## Mapping to the Step 1–5 walkthrough

| Step | Adds | Touches |
|---|---|---|
| 1 | Camera, display loop | `camera.hpp/.cpp`, `main.cpp` |
| 2 | print changed-% per frame pair | `change_detector.hpp/.cpp` |
| 3 | threshold → `CHANGE DETECTED` | `main.cpp` (still no class yet) |
| 4 | 2s-stable → save before/after jpg | `state_machine.hpp/.cpp`, `snapshot_writer.hpp/.cpp` |
| 5 | write `metadata.json` (timestamp, change score, sharpness score) | `snapshot_writer.hpp/.cpp` |

Sharpness score for Step 5 uses `cv::Laplacian` variance (now factored
into the shared `frame_quality` module, since Phase 1.5 needs the same
computation to *choose between* candidates, not just report a number).

## Phase 1.5: ring buffer + best-frame selection

Adds `RingBuffer<T>`, `FrameHistory`, and `frame_quality`. `StateMachine`'s
`CommitEvent` gained `stableWindowStart`/`stableWindowEnd` (the span during
which the desk held still) so `main.cpp` can ask `FrameHistory` for the
sharpest frame in that window instead of settling for whichever frame was
current the instant `WAIT_FOR_STABLE` completed. `before` selection is
untouched — it's already the last frame from well before any change
started, so it isn't subject to the same motion-blur/autofocus risk that
motivated this for `after` (spec section 9).

## Phase 2: raw V4L2 capture

`Camera` no longer uses `cv::VideoCapture`. It speaks V4L2 directly:
`open()` the device node, `VIDIOC_QUERYCAP` to confirm it can stream,
`VIDIOC_S_FMT` to request YUYV at the desired resolution (and read back
whatever the driver actually granted), `VIDIOC_REQBUFS` +
`VIDIOC_QUERYBUF` + `mmap()` to get 4 kernel buffers mapped into our
address space, `VIDIOC_QBUF` to queue them all, then `VIDIOC_STREAMON`.

Each `grab()` is `VIDIOC_DQBUF` (blocks until the driver fills a buffer)
→ convert that buffer's YUYV bytes to BGR via `cv::cvtColor` (this is
also where the data gets copied into memory we own) → `VIDIOC_QBUF` to
hand the buffer back. The convert-before-requeue ordering matters: once
requeued, the driver can overwrite that buffer with the next frame at
any time — the same ownership rule as `FrameHistory::push`'s `.clone()`,
one layer closer to the kernel.

The public `Camera` interface (`grab() -> std::optional<cv::Mat>`) is
identical to Phase 1's; nothing downstream of `Camera` changed.

## Phase 3: multithreading

`main.cpp` now spawns three threads instead of running one loop:

- **Capture thread** owns the moved-in `Camera` exclusively. Each frame
  goes two places: a mutex-guarded `displayFrame` (for the GUI) and
  `BlockingQueue<FrameSample>` (for processing).
- **Processing thread** owns `ChangeDetector`, `StateMachine`, and
  `FrameHistory` — constructed *inside* the thread's own function, so
  nothing outside ever touches them and no locking is needed for any of
  the three. Pops frames, runs the same detect → state machine →
  best-frame pipeline as before, and pushes any resulting `CommitEvent`
  onto a second queue.
- **Storage thread** owns `SnapshotWriter` exclusively. Pops
  `CommitEvent`s and writes them to disk.
- **Main thread** keeps the GUI (`cv::namedWindow`/`imshow`/`waitKey`),
  since OpenCV's highgui isn't guaranteed thread-safe across backends.

The general principle: state that only one thread ever touches needs no
synchronization at all. The two `BlockingQueue`s and the display
`std::mutex` are the *only* three things actually shared between
threads — everything else (`Camera`, `ChangeDetector`, `StateMachine`,
`FrameHistory`, `SnapshotWriter`) is exclusively owned by exactly one
thread, unchanged from earlier phases.

**Shutdown** cascades in one direction: pressing Esc sets an
`std::atomic<bool> running` to false → the capture thread's loop exits
and calls `frameQueue.shutdown()` → the processing thread's
`waitAndPop()` drains what's left, returns `nullopt`, and the thread
exits after calling `commitQueue.shutdown()` → the storage thread drains
and exits the same way → `main` joins all three.

**Timestamps**: `StateMachine::update` now uses each frame's own capture
timestamp (recorded in the capture thread) rather than "now" at
processing time, since the queue between the two threads can add
latency — stability should be measured against when things actually
happened on the desk, not when the processing thread got around to it.

Verified with unit tests (FIFO order, drop-oldest-when-full,
consumer-actually-blocks, shutdown unblocks a waiting consumer, 20,000
items through a concurrent producer/consumer with zero loss) run both
normally and under ThreadSanitizer — no data races reported.

## Phase 4: Git integration

`SnapshotWriter::write` now also runs `git rev-parse HEAD` and
`git status --porcelain` as subprocesses (via `popen`, in `git_info.cpp`)
in the process's current working directory, and records the result in
`metadata.json`:

```json
"git": {
  "available": true,
  "commit": "a91e32f...",
  "dirty_files": ["src/camera.hpp", "src/main.cpp"]
}
```

No `libgit2` dependency — this only runs once per commit (at most every
few seconds), so a subprocess call isn't worth a library for. If the
working directory isn't a Git repo, or `git` isn't installed, both
commands exit non-zero and `captureGitInfo()` returns
`available: false` with nothing else populated — this is supplementary
metadata the rest of the pipeline never depends on, so it fails quietly
rather than treating a missing Git repo as an error.

Verified `captureGitInfo()` directly: run from inside the `palim` repo
(returns the real HEAD commit and the actual dirty files at the time),
and from `/tmp` (a non-repo directory, correctly returns
`available: false`). Also verified the full `metadata.json` output is
valid JSON in both cases.

## Phase 5: V4L2 control metadata

`Camera::currentSettings()` reads `exposure_auto`, `exposure_absolute`,
`gain`, `white_balance_auto`, and `white_balance_temperature` via
`VIDIOC_G_CTRL`, one control at a time, alongside the frame's resolution.
Each control is `std::optional<int>`: a device that doesn't support a
given control (many UVC webcams don't support all of these) leaves it as
`std::nullopt` rather than failing the whole read, and `SnapshotWriter`
renders a missing value as JSON `null`:

```json
"camera": {
  "width": 1280,
  "height": 720,
  "exposure_auto": 1,
  "exposure_absolute": 220,
  "gain": 32,
  "white_balance_auto": 1,
  "white_balance_temperature": 4600
}
```

`Camera` is exclusively owned by the capture thread (Phase 3), so
storage/processing threads can't query it directly. Settings ride along
with each frame instead: `FrameSample` (already carrying a frame and a
timestamp) gained a `CameraSettings settings` field, populated once per
`grab()` in the capture thread, and `StateMachine::update` copies it
straight into the `CommitEvent` it emits. No new synchronization
primitive was needed — this is the same pattern the timestamp already
used.

One simplification: `settings` reflects the frame that triggered the
commit, not necessarily the exact frame Phase 1.5's best-frame selection
later swaps in for `after`. This is fine in practice — exposure/gain/white
balance change far more slowly than frame-to-frame, so re-querying per
candidate during selection wouldn't add meaningful accuracy.

Verified: `currentSettings()` on an unopened `Camera` (no device present)
returns all-`nullopt` controls without crashing; `SnapshotWriter` output
was checked with both a fully-populated `CameraSettings` and an
all-`nullopt` one, confirming valid JSON (with `null` literals, not
missing keys) in both cases.

## Phase 6: timeline CLI (section 16)

The spec itself says the timeline view can start as a CLI — no GUI
framework needed yet. `palim-timeline` is a second, separate executable
(no OpenCV dependency at all) that reads `commits/NNN/metadata.json` and
prints a chronological summary:

```
Experiment Timeline

#001  2026-09-16T00:52:13
  change: 12.3%   sharpness: 0
  camera: 1280x720  exposure=400  gain=16  wb=4600K
  git: 331e6ed (4 dirty files)

        |
        exposure: 400 -> 220
        v

#002  ...
```

Between consecutive commits it also calls out any camera setting that
changed (exposure, gain, white balance) — the "Exposure: 400 → 220"
style annotation from the spec's own example.

`commit_metadata.hpp/.cpp` parses `metadata.json` back into a
`CommitMetadata` struct. This is deliberately *not* a general JSON
parser: it works by searching for each field's unique key name
(`"exposure_absolute":`, `"dirty_files":`, etc.) directly in the file
text, which is only safe because every key `SnapshotWriter` emits is
unique across the whole document — there's no need to track which
nested object (`camera`, `git`) a key belongs to. This is intentionally
narrow: it reads back exactly the shape `SnapshotWriter` produces,
nothing more general.

Verified: generated a synthetic `commits/` directory (via
`SnapshotWriter` directly) with camera settings changing across three
commits, and confirmed `palim-timeline` both displays each commit's data
correctly and detects the between-commit setting changes accurately;
also confirmed the empty-directory and missing-directory cases print a
clear message instead of crashing or printing nothing.

## Phase 6.1: "what changed?" — software side (spec section 4)

`palim-timeline` also diffs consecutive commits' Git state: if the
commit hash moved, it prints `git: <short a> -> <short b>`; if any file
became dirty that wasn't in the previous commit's `dirty_files`, it's
listed under `newly modified`. This is pure comparison of two already-
recorded `metadata.json` snapshots — no `git diff` or other command is
re-run, and there's no attempt to guess *why* something changed (the
spec's "possible cause" idea is real future work, not attempted here).

Physical-side "what changed?" (inferring *what* changed about the desk
itself, not just that the frame differs) stays deferred — it needs
either object detection or a human looking at `before.jpg`/`after.jpg`,
neither of which this phase adds.

Verified: two synthetic `metadata.json` pairs — one with a different
commit hash and a newly-dirty file (confirms the diff renders exactly
those two things), and one that's identical (confirms it correctly
prints nothing rather than a false-positive diff).

## Phase 7: GTest suite (section 19)

Every scratch/ad-hoc test used to verify earlier phases (frame-diff math,
`StateMachine` transitions, `RingBuffer` wraparound, `FrameHistory`
best-frame selection, `BlockingQueue` concurrency, `GitInfo`,
`commit_metadata` parsing, `Camera`'s failure paths) has been ported
into a permanent GTest suite under `tests/`, built as `palim_tests` and
registered with CTest via `gtest_discover_tests`.

This required one structural change: `main.cpp` and `timeline_main.cpp`
used to each list every `.cpp` file they needed directly. Now all of
that logic (everything except the two `main()`s) lives in a static
library, `palim_lib`, that `palim`, `palim-timeline`, and `palim_tests`
all link against — so the same code isn't compiled three times, and
tests exercise the exact objects the real binaries ship.

Notable test design points:

- `BlockingQueue` tests include real concurrency (a consumer thread that
  genuinely blocks until `push()`, a 5000-item concurrent
  producer/consumer with an ordering + no-loss check) — passing both
  normally and under ThreadSanitizer.
- `GitInfo` tests change the process's working directory (`git` runs
  against cwd) and restore it via an RAII guard, so a failed assertion
  can't leave a later test running from the wrong directory.
- `Camera` tests only cover failure paths (no device, a non-V4L2 device)
  since no physical camera is available in this environment;
  `Camera::grab()` against real hardware is explicitly *not* covered
  here and needs manual verification once real hardware is available.

`-DPALIM_BUILD_TESTS=OFF` skips building the suite (and its GTest
dependency) entirely, for anyone who just wants the two runtime
binaries.

## Phase 8: changed-region localization — physical side, step one (spec section 4)

Phase 6.1 diffed the *software* side (Git state) between commits. The
physical side was left fully deferred because "what changed on the
desk" sounds like it needs object recognition. It doesn't, not for a
useful first step: `ChangeDetector` already computes a per-pixel diff
mask to get its percentage — it was just throwing away *where* those
pixels were. `cv::boundingRect()` on that same mask gives a bounding
box around the changed area for free, no new model or dependency.

`ChangeDetector` gained `compareWithRegion()` alongside the existing
`compare()` (which keeps its original signature and behavior
unchanged — nothing that called `compare()` needed to change). Both
share a new private `changedMask()` helper that factors out the
preprocess → absdiff → threshold steps `compare()` already did.
`compareWithRegion()` additionally returns the mask's `boundingRect()`
as `std::optional<cv::Rect>`, left unset when nothing crossed the
threshold (an all-zero mask's bounding rect is the meaningless
`Rect(0,0,0,0)`).

The region flows through the rest of the pipeline the same way the
change percentage already did: `main.cpp`'s processing loop now calls
`compareWithRegion()` and passes the resulting region into
`StateMachine::update()`, which stores it on the `CommitEvent` it
eventually emits — specifically, the region from the exact tick whose
stable-window wait completes and triggers the commit, not an earlier
or later one. `SnapshotWriter` writes it into `metadata.json` as a
`changed_region` object (`region_x`/`region_y`/`region_w`/`region_h`,
or `null` when there wasn't one) and, when present, also writes
`after_annotated.jpg` — the "after" frame with the region drawn on it
in red, so a human can see at a glance where `palim` thinks the change
was without opening an image viewer and comparing two files by eye.

The `region_*` key names are deliberate, not cosmetic: `commit_metadata`'s
JSON reader finds `"key":` anywhere in the whole document text rather
than tracking which object it's nested in, so reusing `x`/`y`/`width`/
`height` here would collide with the `camera` block's own `width`/
`height`. `commit_metadata.hpp` represents a parsed region as a plain
`ChangedRegion{x, y, width, height}` struct rather than `cv::Rect`,
keeping `commit_metadata`/`palim-timeline` free of an OpenCV dependency
they've never needed. `palim-timeline` prints the region (position and
size) under each commit that has one.

Older `metadata.json` files written before this phase have no
`changed_region` key at all; the parser treats a missing key exactly
like an explicit `null` — both become `std::nullopt` — so existing
commit histories keep reading correctly.

Verified: `ChangeDetectorRegion` tests cover identical frames (no
region), a region-less frame reporting the same percentage
`compare()` would, and a change confined to one corner producing a
tight bounding box close to that corner rather than the whole frame.
`StateMachineTest` confirms a `CommitEvent` carries the region from its
*triggering* tick specifically, not an earlier changing-phase tick.
`CommitMetadataTest` covers a populated region, an explicit `null`,
and a missing key, the last guarding backward compatibility with
pre-Phase-8 commits. All passed; deliberately breaking
`compareWithRegion()`'s region assignment (disabling it unconditionally)
caused exactly `ChangeDetectorRegion.LocalizedChangeReportsATightBoundingBox`
to fail, confirming the test is actually exercising the new code path,
not passing vacuously.

This is a bounding box, not object recognition — it answers "roughly
where" but not "what" the changed object is. That's still future work;
see below.

## Phase 9: CLI configurability

Every tunable `main()` actually had — camera device path, capture
resolution, the change-threshold percentage, the stable-duration
window, the commits output directory, and (as of this phase's third
round, below) `FrameHistory`'s capacity — was a hardcoded constant or
literal, even though every class involved (`Camera`, `StateMachine`,
`SnapshotWriter`, `FrameHistory`) already took these as constructor
parameters. Changing any of them meant editing `main.cpp` and
recompiling.

A new `Config` struct (`src/config.hpp`/`.cpp`) holds all six fields
with their previous hardcoded values as defaults, and a free function
`parseArgs(const std::vector<std::string>&)` builds one from CLI
arguments: `--device`, `--width`, `--height`, `--threshold`,
`--stable-ms`, `--output-dir`, `--frame-history-capacity`. It depends
on neither OpenCV nor any other `palim` header, so it's just as
cheaply unit-testable as `commit_metadata`. `main()` calls it on
`argv` before touching the camera, camera/state-machine/snapshot-
writer/frame-history construction all read from the resulting
`Config`, and an invalid argument (unrecognized flag, missing value,
a value that doesn't fully parse as a number, or one that parses but
is out of range) prints a usage message and exits instead of letting
an uncaught exception propagate, silently misparsing the value, or
silently accepting a value that would misbehave later.

That "fully parse" qualifier matters: `std::stoi`/`std::stod` silently
accept a numeric prefix and ignore trailing garbage (`"640x480"` ->
`640`) unless the caller checks how many characters were actually
consumed. `parseArgs` checks this for every numeric flag.

"Out of range" isn't just pickiness, either — a second round on this
phase added `parseIntAtLeast()` (used for `--width`/`--height` with a
minimum of 1, `--stable-ms` with a minimum of 0, and
`--frame-history-capacity` with a minimum of 1) and
`parseNonNegativeFiniteDouble()` (used for `--threshold`), because
each flag's naive parse let through a value that was syntactically
fine but semantically dangerous: a non-positive `--width`/`--height`
would get cast to a huge unsigned value in `Camera::setFormat`'s
V4L2 request; a non-positive `--frame-history-capacity` would reach
`RingBuffer`'s wraparound indexing, which divides by its own capacity
— a modulo-by-zero on the very first frame pushed, not a theoretical
concern; a negative `--stable-ms` would make `StateMachine`'s
`now - stableSince_ >= stableDuration_` check true almost immediately,
committing on essentially the next tick instead of waiting for real
stability; and `std::stod` happily parses `"nan"`/`"inf"`/`"-inf"` (and
a plain negative number) as `--threshold`, none of which mean anything
as a change-percentage.

Verified: `ParseArgs` tests cover the all-defaults case, each flag
individually, all flags together, an unrecognized flag, a flag missing
its value, a non-numeric value, a numeric value with trailing garbage
(`"640x480"`, `"5percent"`) to guard the `std::stoi`/`std::stod`
partial-parse trap above, and each flag's own invalid-range values
(non-positive width/height/frame-history-capacity, negative
stable-ms, non-finite or negative threshold) alongside the boundary
itself staying accepted (`--stable-ms 0`, `--threshold 0`). All
passed; deliberately removing the consumed-length check on the integer
parser caused exactly `ParseArgs.TrailingGarbageAfterNumberThrows` to
fail, and separately removing `parseIntAtLeast`'s bound caused exactly
its own dependent tests (width/height, stable-ms, frame-history-
capacity) to fail — confirming each test actually exercises its own
check rather than passing vacuously.

## Phase 10: clean shutdown on SIGINT/SIGTERM

`main()`'s three threads (capture, processing, storage) already shut
down cleanly on their own `running`-flag protocol — but the only way
to set that flag was pressing Esc in the preview window. Ctrl-C
(`SIGINT`) or a plain `kill` (`SIGTERM`) hit the default handler
instead, which just terminates the process immediately. For most
programs that's a minor inconvenience; for this one specifically it's
a correctness problem, because the storage thread can be mid-
`SnapshotWriter::write()` at any moment — `before.jpg` written,
`after.jpg` not yet, `metadata.json` not yet either. A signal landing
there would leave that exact half-written `commits/NNN/` directory
behind: a corrupted commit in a tool whose entire point is that a
commit, once written, is a trustworthy record.

`src/shutdown_flag.hpp` adds `ShutdownFlag`, a process-wide singleton
around one `std::atomic<bool>`, and `installShutdownSignalHandlers()`,
which registers a handler for `SIGINT`/`SIGTERM` that calls its
`request()`. The handler itself does nothing but that one atomic
store — a signal handler can only safely call a small, fixed set of
library functions, and a relaxed store/load on an already-lock-free
atomic is one of the few operations actually guaranteed safe inside
one. `main()`'s old local `running` flag is gone; every loop that used
to read it (`captureThread`'s grab loop, the main thread's display
loop) now reads `ShutdownFlag::instance().requested()` instead, and
the Esc-key path calls `request()` instead of storing into a local —
otherwise the shutdown sequence (capture stops → `frameQueue.shutdown()`
→ processing drains and stops → `commitQueue.shutdown()` → storage
drains and stops → all three threads joined) is exactly what it always
was.

Verified: `ShutdownFlag` tests cover `request()`/`requested()`
directly, and — the pair that actually matters — installing the real
handler and calling `raise(SIGINT)`/`raise(SIGTERM)` (not just calling
`request()` directly) to confirm the flag ends up set through the
full signal-delivery path, the same path `main()` itself relies on.
All passed; temporarily making the handler a no-op caused exactly
those two signal-delivery tests to fail (the two direct-call tests
still passed, as expected), confirming they actually exercise signal
delivery rather than passing vacuously. Restored, full suite (64
tests) green again.

## Phase 11: `SnapshotWriter` resumes numbering instead of overwriting old commits

`SnapshotWriter` numbered commits sequentially starting from 1 every
time it was constructed — fine for a single run, but it had no memory
of what a *previous* run already wrote. Restarting `palim` against the
same `commits/` directory (after a crash, a planned restart, anything)
started counting from 1 again and silently overwrote `commits/001/`,
`002/`, and so on with brand-new before/after/metadata — destroying
whatever physical-change history was already recorded there. For a
tool whose entire premise is that a written commit is a trustworthy
record, this was the most damaging bug found so far: not a narrow
race window like Phases 9-10's fixes, but a plain, 100%-reproducible
data-loss path on the single most ordinary event this program will
ever see — being restarted.

Added `findNextAvailableId()` (`src/snapshot_writer.cpp`): scans
`outputDir` for existing subdirectories whose name is entirely digits
(what this class itself ever creates, e.g. `"007"` — anything else,
a stray file or a directory a human added, is ignored rather than
treated as a parse error) and returns one past the highest ID found,
or `1` if none exist. The constructor now calls this instead of always
starting at `1`, so a second `SnapshotWriter` built against a
directory that already holds commits 1-3 resumes at 4.

This was also `SnapshotWriter`'s first real test coverage at all —
previously exercised only indirectly through `main.cpp`, which no test
here touches. `tests/test_snapshot_writer.cpp` builds synthetic
`CommitEvent`s (plain solid-color frames, no camera needed) and
covers: a fresh directory starting at `001`; several commits in one
run incrementing sequentially; the actual bug this phase fixes —
writing 1-3, marking commit `002` with a sentinel file, constructing a
*second* `SnapshotWriter` against the same directory, writing again,
and confirming the new commit lands at `004` while `002`'s sentinel
file is untouched; and a non-numeric subdirectory being ignored rather
than breaking the scan.

**Verified**: all four new tests passed. Temporarily hardcoding
`findNextAvailableId()` to always return `1` (restoring the original
bug) caused exactly the two tests that depend on resumption
(`ResumingInAnExistingDirectoryDoesNotOverwritePriorCommits`,
`IgnoresNonNumericSubdirectories`) to fail, while the two single-run
tests kept passing (as expected, since they never restart a writer) —
confirming the new tests actually exercise the fix rather than passing
vacuously. Restored, full suite (68 tests) green again.

## Phase 12: the capture thread gives up instead of busy-spinning on a dead camera

The capture thread's loop was `auto frame = camera.grab(); if (!frame)
{ continue; }`. On the success path that's fine — `grab()` blocks on
`VIDIOC_DQBUF` until the driver has a real frame ready, so the loop
naturally paces itself at the camera's own frame rate. On the failure
path there's no such blocking: a V4L2 ioctl that's going to fail
typically fails immediately, so a disconnected camera or a stuck
driver turned this `continue` into an unthrottled busy loop, spinning
one CPU core at 100% forever with no error message and no way for the
rest of the program (or the user) to notice anything was wrong short
of the preview window visibly going stale.

Added `src/consecutive_failure_guard.hpp`: `ConsecutiveFailureGuard`
tracks consecutive failures of some operation and reports (via
`recordFailure()`'s return value) once enough of them have happened in
a row, with no `recordSuccess()` in between, to treat the condition as
permanent rather than a transient blip. Pure counting logic, no OpenCV
or camera dependency, so it's unit-testable the same way
`commit_metadata`/`Config` already are.

The capture thread now wraps `camera.grab()` with one
(`kMaxConsecutiveGrabFailures = 30`): a failed grab sleeps 10ms (bounds
the spin to a sane rate instead of pegging the CPU, even during the
~300ms it takes to reach the threshold) and retries; reaching the
threshold logs why and calls `ShutdownFlag::instance().request()` —
the same clean-shutdown path Phase 10 already wired up for `SIGINT`/
`SIGTERM` — instead of spinning forever. A single dropped frame still
costs nothing but one 10ms sleep, matching the project's own
established tolerance for occasional frame drops elsewhere (the frame
queue's own bounded-drop behavior, Phase 3).

Verified: `ConsecutiveFailureGuard` tests cover not giving up before
the threshold, giving up exactly at it, a success resetting the count,
a threshold of 1 giving up on the very first failure, and staying
given up on further failures past that point. All passed; temporarily
making `recordFailure()` always return `false` (never giving up)
caused exactly the four tests that depend on the threshold actually
firing to fail, while the one test that never expects `true`
(`DoesNotGiveUpBeforeThreshold`) kept passing — confirming the others
actually exercise the threshold logic rather than passing vacuously.
Restored, full suite (73 tests) green again. The capture-thread wiring
itself can't be exercised live in this headless container (no real
camera to disconnect) — verified by code review, the same limitation
`Camera::grab()`'s own tests already have.

## Phase 13: `commit_metadata.cpp`'s JSON reader now counts escapes correctly

`extractRawValue`'s and `extractStringArray`'s string-value parsing
found a closing quote by searching for the next `"` and bailing out if
the single character right before it was a backslash — on the theory
that a backslash immediately before a quote means that quote is
escaped (`\"`, part of the string's content, not the delimiter). That
theory breaks for a value ending in a literal backslash: `commit`,
`available`, and each entry of `dirty_files` are strings
`SnapshotWriter::jsonEscape()` wrote, and it escapes a literal `\` the
same way standard JSON does, by doubling it to `\\`. A dirty file path
ending in one literal backslash is written as `...\\"` — two
backslashes, then the real closing quote — and the old single-character
check saw that second backslash and wrongly concluded the quote right
after it was escaped too, sending the search past the string's actual
end and into whatever came next in the document (the next array entry,
or the next key entirely). Separately, neither function ever reversed
`jsonEscape()`'s escaping on the way out, so even a value whose closing
quote *was* found correctly would come back with stray backslashes
still embedded in front of every `"` or `\` it ever contained.

Added two helpers: `findClosingQuote()` counts the full run of
consecutive backslashes immediately before each candidate closing
quote rather than just the one before it — an odd count means the
quote is escaped (keep searching), an even count (including zero)
means it's the real delimiter — and `jsonUnescape()` reverses exactly
the two escapes `jsonEscape()` ever writes (`\"` and `\\`) so a
round-tripped value comes back exactly as it went in. Both
`extractRawValue`'s string branch and `extractStringArray`'s inner
loop now go through these instead of the old naive check.

Verified: added `DirtyFileEndingInLiteralBackslashDoesNotCorruptParsing`
(a `dirty_files` array with an entry ending in a literal backslash,
followed by a second ordinary entry) and
`DirtyFileWithEmbeddedQuoteIsUnescaped` (an entry containing a literal
`"`) to `tests/test_commit_metadata.cpp`. Temporarily reverting
`findClosingQuote()` to the old single-character check reproduced the
bug exactly as described — the backslash test failed with
`dirtyFiles` showing the parse having run straight through the array's
real end into the raw bytes of the next field — while the quote test
still passed (that one exercises `jsonUnescape()`, which the revert
didn't touch) and the rest of the suite was unaffected. Restored, full
suite (75 tests) green again.

## Phase 14: `changeScore`/`changedRegion` now actually reflect the change that triggered the commit

`CommitEvent::changeScore`'s own field comment has said "the changed-%
reading that triggered CHANGING" since the very first commit that
introduced it (Phase 3) — but `StateMachine::update()` never actually
read that value off the `Stable -> Changing` transition. It built the
`CommitEvent` out of whichever `changedPercent`/`changedRegion` were
passed to the *final*, stability-confirming tick instead — the one
where `now - stableSince_ >= stableDuration_` finally holds. That tick
is stable by definition, so its `changedPercent` is always at or below
`changeThresholdPercent_` — meaning every `change_score` this project
has ever written to a `metadata.json` understated the actual change
that happened, usually down near zero, regardless of whether the
desk's change was tiny or dramatic. README's own example metadata
(`"change_score": 42.5`) only makes sense under the comment's original
intent; the actual value in a fresh commit's own `metadata.json` would
never look like that.

The regression hid for two reasons. First, there was nothing to notice
it against: `change_score` has no consumer that validates it (`palim-
timeline` just prints whatever number is there), so a wrong-but-
plausible-looking small number never looked broken. Second, Phase 8
(changed-region localization) extended the same pattern to
`changedRegion` and its own journal entry even describes it as
deliberate — "the region from the exact tick whose stable-window wait
completes and triggers the commit, not an earlier or later one" — which
reads as a design choice, not as "matches changeScore's pre-existing
bug." But the same reasoning applies: a stable tick's own changed
pixels, when there are any, are incidental sensor noise around the
threshold, not the region of the actual physical change — so
`after_annotated.jpg`'s whole point (showing a human at a glance where
`palim` thinks the desk changed) was being undermined by the same gap,
silently, on every single annotated commit.

Fixed by capturing both values once, at the same `Stable -> Changing`
transition that already captures `beforeFrame_` (`triggerChangeScore_`/
`triggerChangedRegion_` in `state_machine.hpp`), and building the
eventual `CommitEvent` from those instead of the final tick's
parameters. Motion resuming mid-`WaitForStable` (back to `Changing`,
without passing back through `Stable`) doesn't re-capture them, for the
same reason it doesn't overwrite `beforeFrame_` either: both describe
the change that originally opened this commit cycle, not whichever
tick happens to eventually confirm it settled down.

Verified: `CommitsOnceStableDurationElapsesAfterChange` previously
asserted `changeScore == 0.0` (the stable tick's own reading) — updated
to assert the real trigger value, `20.0`.
`CommitEventReflectsARegionOnTheExactTriggeringTick` previously passed
its test region on the *final* tick and called that "the triggering
tick" (the bug's own framing); renamed to
`CommitEventReflectsTheRegionFromTheTriggeringTick` and rewritten to
pass the region on the actual `Stable -> Changing` tick instead, with
the final tick carrying none. Added
`ResumedMotionDoesNotOverwriteTheOriginalTrigger`, mirroring the
existing `beforeFrame_`-preservation coverage for both new fields
together. All three pass. Temporarily reverting `update()` to build
the `CommitEvent` from `changedPercent`/`changedRegion` directly (the
original bug) reproduced it exactly: those same three tests failed
with the predicted wrong values (`changeScore` reading `0` instead of
`20`, both region checks reading `nullopt`), while the rest of the
suite was unaffected. Restored, full suite (76 tests) green again.

## Phase 15: corrected `BlockingQueue`'s own stale comment about the commit queue

A documentation-only fix this time, found while re-reading every file
for a genuine change and turning up nothing in the code itself.
`BlockingQueue`'s class comment (`src/blocking_queue.hpp`, present
since the very commit that introduced both this class and `main.cpp`'s
three-thread pipeline) said passing capacity `0` for an unbounded queue
is "used for the commit queue, where items are rare and none should
ever be dropped." `main.cpp` has never actually done that: its commit
queue (`kCommitQueueCapacity`) has always been a generous but finite
`64`, not `0` — a real difference, since a capacity of `64` *can* still
drop an item under a sufficiently pathological stall (a storage thread
stuck long enough for 64 commits to queue up behind it), which capacity
`0` structurally cannot.

Reworded the comment to describe what `main.cpp` actually does for the
commit queue — a large finite capacity, generous enough to never
realistically fill, rather than the unbounded option — instead of
misdescribing it as the thing the queue's own `0`-capacity option is
for. The `0`-capacity mechanism itself was already correct and already
tested (`tests/test_blocking_queue.cpp`'s capacity-`0` cases); only the
comment's claim about where it's actually used was wrong.

Verified: comment-only change, so no new test — rebuilt clean and ran
the full suite (76 tests) to confirm nothing else was affected.

## Phase 16: `StateMachine`'s very first tick could crash the storage thread

A genuine crash this time, not a narrow preemption race: `StateMachine`
remembers the previous frame it was shown (`lastFrame_`) so that, when
a `Stable -> Changing` transition fires, `beforeFrame_` can be set to
"whatever the desk looked like just before this change started."
`lastFrame_` starts out as a default-constructed, empty `cv::Mat`, and
nothing stopped the *very first* call to `update()` — before
`lastFrame_` has ever been assigned a real frame — from reading as
`changing` and capturing that empty Mat as `beforeFrame_` anyway.
Every test in `tests/test_state_machine.cpp` happened to call `tick()`
with a non-changing percentage first (mirroring ordinary operation,
where the first couple of frames are rarely already wildly different),
so none of them ever exercised the one call where it actually matters.

This is concretely reachable, not theoretical: a camera's auto-
exposure or auto-white-balance can still be settling across its first
few real frames, producing a frame-to-frame difference large enough to
cross `changeThresholdPercent_` before anything has genuinely "changed"
on the desk — and if the desk already has something moving in frame
the moment `palim` starts, the first real comparison can exceed the
threshold outright. Either way, once that cycle eventually stabilizes
and commits, `SnapshotWriter::write()` calls `cv::imwrite()` on an
empty `before` Mat, which throws — uncaught inside the storage
thread's own lambda, so it takes the whole process down via
`std::terminate()`.

Fixed with one additional condition: `haveBaseline` (`!lastFrame_.empty()`),
checked alongside `changing` before allowing a `Stable -> Changing`
transition at all. The very first call, with no real baseline yet,
is now absorbed as pure initialization — `lastFrame_` gets set to a
real frame at the end of the function either way — and can't start a
commit cycle no matter how large its own `changedPercent` reads. Every
call from the second one onward behaves exactly as before.

Verified: added `FirstEverTickCannotStartAChangeCycleWithNoBaselineFrame`
(first tick already "changing," confirmed via the fixture's single
shared `cv::Mat` that a commit would otherwise carry an empty `before`)
to `tests/test_state_machine.cpp`. Before the fix, this test failed
exactly as predicted — a commit fired from that first tick's cycle.
Temporarily reverting the `haveBaseline &&` check reproduced the
failure again, isolated to this one test; the rest of the suite was
unaffected either way. Restored, full suite (77 tests) green again.

## Phase 17: `--max-commits` bounds disk usage instead of growing forever

Every genuinely similar tool — timelapse cameras, motion-triggered
security recorders, 3D-printer watch-and-log utilities — has some form
of retention policy, because the one thing they all have in common
with `palim` is running unattended, potentially for days or weeks, each
commit writing two JPEGs (plus an optional third, `after_annotated.jpg`)
to disk. `palim` had no such thing: `commits/` only ever grew, with
nothing to stop an unattended run from eventually filling the disk.
Phase 11 made restarting safe (resuming numbering instead of
overwriting); this is the complementary problem — making *staying up
indefinitely* safe too.

Added `SnapshotWriter`'s `maxCommits` constructor parameter (default
`0`, meaning unlimited — this project's exact behavior before this
phase) and a matching `--max-commits COUNT` CLI flag
(`Config::maxCommits`, validated the same way `--frame-history-capacity`
already is: `parseIntAtLeast(..., 1)`, since `0`'s meaning is reserved
for the default rather than something a user passes explicitly).
`write()` now calls a new `pruneOldCommits()` after every commit: if
more than `maxCommits` commit directories exist, it deletes the
oldest (lowest-numbered) ones — via `std::filesystem::remove_all()`,
so a pruned commit's `before.jpg`/`after.jpg`/`metadata.json` are
actually gone, not an orphaned empty directory — until back at the
cap. A no-op, as cheaply as a single size comparison, whenever the cap
isn't set or hasn't been reached yet.

`findNextAvailableId()`'s own directory scan (Phase 11) and this new
pruning both need the same thing — every numeric-named subdirectory,
sorted by ID — so that scan was factored out into a shared
`listCommitDirs()` returning `(id, path)` pairs ascending (oldest
first): `findNextAvailableId()` reads the last entry for the highest
ID, `pruneOldCommits()` deletes however many of the first entries are
over the cap. Lowering the cap against a directory that already has
more commits than the new limit (e.g. a backlog from an earlier,
unlimited run) isn't trimmed retroactively on construction — only the
*next* `write()` triggers `pruneOldCommits()`, which then deletes
however many it takes to reach the cap in that one pass, not one
write() at a time. Numbering itself is never affected by pruning:
`nextId_` only ever increments, so the ID sequence stays the true
count of commits ever written, regardless of how many of the earlier
ones have since been deleted.

Verified: added `DefaultMaxCommitsOfZeroNeverPrunes`,
`WritingPastMaxCommitsDeletesTheOldestFirst`,
`LoweringTheCapTrimsAnExistingBacklogOnTheNextWrite`,
`PruningActuallyRemovesTheDirectoryContents`, and
`PruningDoesNotResetTheIdSequence` to `tests/test_snapshot_writer.cpp`,
plus `ParseArgs` cases for `--max-commits`' override and its
non-positive rejection. Temporarily short-circuiting
`pruneOldCommits()` to always return early reproduced the predicted
failures exactly — all four pruning-dependent tests failed, while the
unlimited-default test and all four pre-existing (pre-Phase-17)
`SnapshotWriterTest` cases stayed green — confirming the new tests
actually exercise pruning rather than passing vacuously. Restored,
full suite (84 tests) green again.

## Phase 18: `git status --porcelain` parsing is now unit-tested against synthetic input

Not a bug, not a feature — a real gap in test coverage, found while
re-reading every file for something genuine. Phase 4's own `GitInfo`
tests (`tests/test_git_info.cpp`) only ever ran `captureGitInfo()`
against the real filesystem: once from inside this actual repo
(asserting only that `available` is true and the commit hash is 40
hex characters — never anything about `dirtyFiles`' actual content)
and once from `/tmp` (asserting the all-empty "not a repo" case). The
line-parsing logic that turns `git status --porcelain`'s raw text into
`dirtyFiles` — skip the 2-character status code and the space after
it, one entry per line — had no test exercising it with controlled
input at all. Whatever this repo's own working tree happened to look
like at test time was the only input it was ever checked against.

Pulled that parsing out of `captureGitInfo()`'s body into its own
function, `parseDirtyFiles(const std::string& porcelainOutput)`
(declared in `git_info.hpp`, so it's reachable from tests the same way
`ChangeDetector`/`StateMachine`/`commit_metadata`'s own pure logic
already is) — a plain string-in, `vector<string>`-out function with no
subprocess, no filesystem, and no real Git repository involved.
`captureGitInfo()` now just calls it on whatever `git status
--porcelain` actually returned.

Added `tests/test_git_info.cpp` cases covering: empty output; ordinary
modified/added/untracked lines (` M`, `A `, `??` — the same 2-character-
code-plus-space shape); a rename line (`R  old -> new`), confirming it
comes through as one combined `"old -> new"` string rather than being
split into two paths (documenting the current behavior precisely,
not claiming it's the only sensible one — splitting renames into two
entries is a separate, deliberate design decision for another day, not
something to silently change while just adding coverage); a trailing
`\r` being stripped; and lines too short to contain a path being
skipped rather than producing a bogus empty or truncated entry.

Verified: all five new tests pass against the real implementation.
Two independent, deliberately narrow breaks each reproduced exactly
one predicted failure and nothing else: removing the `rstrip()` call
(keeping the length guard) failed only `StripsATrailingCarriageReturn`;
separately replacing the length guard with an unconditional, bounds-safe
substring (so every line — including the too-short ones — produced an
entry) failed only `SkipsLinesTooShortToContainAPath`. Both reverted,
full suite (89 tests) green again.

## Phase 19: `palim-timeline`'s own output formatting is now unit-tested

The biggest remaining test-coverage gap, found the same way Phase 18's
was: `timeline_main.cpp` had no test file at all, for any of it.
Phase 6's own "Verified" paragraph describes generating a synthetic
`commits/` directory and checking `palim-timeline`'s output *by eye*
("confirmed `palim-timeline` both displays each commit's data correctly
and detects the between-commit setting changes accurately") — real
verification at the time, but never captured as a repeatable test.
Every formatting decision since (what `?` means for an unsupported
camera control, exactly which characters a hash gets truncated to,
that a file going from dirty back to clean is deliberately *not*
reported) existed only as prose in a comment, not as something a
future change could accidentally break without anyone noticing.

The obstacle was structural: `shortHash()`, `printOptionalIntChange()`,
`printGitChange()`, and `printCommit()` all lived in `timeline_main.cpp`,
printing straight to `std::cout` — and that file also defines `int
main()`, so nothing in it can be linked into `palim_tests` without a
duplicate-`main` clash.

Moved all four into a new `src/timeline_format.hpp`/`.cpp` (added to
`palim_lib`, the same static library every other testable piece of
logic already lives in), renamed to the `format*` verb to match their
new return-a-string shape (`formatShortHash`, `formatOptionalIntChange`,
`formatGitChange`, `formatCommit`), and converted from printing
directly to building an `std::ostringstream` and returning its `.str()`.
`timeline_main.cpp` now does nothing but call `listCommits()` and
`std::cout <<` each function's result in the same order as before — a
pure refactor, not a rewrite: verified by capturing `palim-timeline`'s
real output against a synthetic two-commit directory (differing
exposure, a moved Git commit, a newly-dirty file) both before and
after the change and diffing them — byte-for-byte identical.

Added `tests/test_timeline_format.cpp`: `formatShortHash` truncating
correctly, including hashes already short enough; `formatOptionalIntChange`
being empty when nothing changed and using `?` for a `std::nullopt`
side; `formatGitChange` being empty with no Git on either side or an
identical commit and dirty-file list, reporting a moved commit hash,
reporting a newly-dirty file, and — the one behavior worth pinning
down precisely — *not* reporting a file that went from dirty back to
clean, since this is deliberately a one-directional "what got worse"
diff, not a symmetric one; and `formatCommit` covering the region line,
optional camera controls appearing only when present, and the Git
summary line appearing only when available.

Verified: all 16 new tests pass. Temporarily changed `formatGitChange`'s
newly-dirty computation to `after.dirtyFiles` directly (as if every
file in the *after* list were newly dirty) and confirmed exactly the
two tests depending on that distinction —
`ReportsNewlyDirtyFiles` and `DoesNotReportFilesThatBecameClean` —
failed, while every other test (including the Git-availability and
identical-state cases) stayed green. Restored, full suite (105 tests)
green again, and `palim-timeline`'s real output re-confirmed
byte-for-byte identical to the pre-refactor baseline once more.

## Phase 20: `computeSharpness` gets its own direct tests

Another gap found the same way Phases 18 and 19's were: `frame_quality.cpp`
has exactly one function, `computeSharpness` — Laplacian variance, used
by both `SnapshotWriter` (records the number in `metadata.json`) and
`FrameHistory` (picks the sharpest of several candidate frames) — and it
had no test file of its own. `FrameHistoryTest` exercises it, but only
*relatively*: every candidate frame there has random noise added (even
the "sharpest" index), so those tests only ever check that one noisy
frame scores higher than another, never the function's own documented
contract in isolation ("variance = sharpness score", "higher means more
in-focus").

Added `tests/test_frame_quality.cpp`: a perfectly uniform frame (no
noise at all) scores exactly `0.0` — the one input where the expected
answer isn't "some number bigger/smaller than some other number" but a
precise value, since a constant image's Laplacian is zero everywhere;
this held at both ends of the shade range (`0` and `255`), not just one
arbitrary value. A frame with real edge content (a black/white split)
scores strictly above `0`. Blurring that same edge frame with
`cv::GaussianBlur` — softening the exact feature this score is meant to
detect — lowers its score below the unblurred original's.

Verified: temporarily changed the return value to the Laplacian's mean
magnitude instead of its variance (`std::abs(mean[0])` instead of
`stddev[0] * stddev[0]`) — still zero for a perfectly flat region (a
constant image's Laplacian mean is also zero, so the uniform-frame tests
stayed green, as predicted), but a sharp edge's positive and negative
Laplacian responses mostly cancel out in the mean, so both the
positive-sharpness and the blur-lowers-sharpness tests failed exactly as
expected, with nothing else affected. Restored, full suite (109 tests)
green again.

## Phase 21: `palim-timeline` listed commits out of order past #999

`SnapshotWriter::write()` names each commit directory with
`std::setw(3)` zero-padding ("001", "002", ...) — but `setw` only sets
a *minimum* field width; it doesn't truncate or widen the padding once
an ID genuinely needs more digits than that. "1000" is 4 characters,
not "1000" re-padded to stay ahead of "999". `commit_metadata.cpp`'s
`listCommits()` sorted those directory names with plain
`std::sort(dirs.begin(), dirs.end())` — lexicographic path comparison
— relying on a comment's own assumption ("zero-padded names ->
lexicographic == numeric") that only holds while every ID shares the
same width. Past 999 commits, it doesn't: `"1000" < "999"` as strings
(`'1' < '9'`), the opposite of their actual numeric order.

Concretely reachable, not a theoretical edge case: this is exactly the
"left running unattended" scenario `--max-commits` (Phase 17) exists
for. Without that flag set, commits accumulate without bound, and a
long enough run crosses the 999 boundary — at which point every commit
from #1000 onward would display *before* #998/#999 in
`palim-timeline`'s listing, scrambling the very timeline this tool
exists to produce.

Confirmed before fixing it: built a synthetic `commits/` directory with
`998`, `999`, `1000`, `1001` and called `listCommits()` directly — it
returned `1000, 1001, 998, 999`, not `998, 999, 1000, 1001`.

Added `parseCommitId()` (the same digit-only-name-to-int parsing
`SnapshotWriter`'s own `listCommitDirs()` already does, for the same
reason) and changed the sort to compare by parsed numeric ID when both
sides have one, falling back to path comparison only for whatever
doesn't parse (a stray non-numeric directory) so the sort stays a
strict ordering either way. This is correct no matter how many digits
a commit ID ever grows to, not just safe up to some larger but still
eventually-reachable padding width.

Verified: added `ReturnsCommitsInNumericOrderAcrossDifferentIdWidths`
(the same 998/999/1000/1001 case). Temporarily reverted the sort to
plain lexicographic path comparison — confirmed exactly this test
failed, reproducing the predicted `1000, 1001, 998, 999` order, while
the pre-existing same-width `ReturnsCommitsInNumericOrder` test (which
never exercises the width mismatch) stayed green. Restored, full suite
(111 tests) green again, and re-ran the real `palim-timeline` binary
against the same synthetic 998/999/1000/1001 directory: output now
lists `#998`, `#999`, `#1000`, `#1001` in that order.

## Phase 22: `--help`/`-h` instead of an "unrecognized argument" error

Found by checking `palim` against the exact gap `focus-monitor` (the
project this account also maintains) had just been found to have:
`parseArgs()` had no case for `--help`/`-h` at all, so it fell through
to the catch-all `throw std::invalid_argument("unrecognized argument:
--help")`, which `main()` reported as `Argument error: unrecognized
argument: --help` on stderr with exit code `1` — the single most
common thing a new user types, treated as a failure rather than the
always-available, non-error request every other command-line tool
treats it as.

Added `Config::helpRequested` (default `false`) and a scan for
`--help`/`-h` at the very start of `parseArgs()`, before any other
argument is even looked at — set and return immediately when found,
so `--help` wins regardless of what else is on the command line (an
otherwise-invalid flag like a bare `--width` with no value no longer
matters once `--help` is also present, in either order). `main()`
checks `config.helpRequested` right after `parseArgs()` returns, ahead
of its existing error-handling path, and prints the usage to stdout
with exit `0` instead of stderr with exit `1`. The usage string itself
moved into a small `printUsage()` so the error path and the help path
can never list a different set of flags from each other.

Verified: confirmed against the real binary first — `palim --help`,
`palim -h`, and `palim --width --help` (an otherwise-incomplete flag
alongside `--help`) all now print the usage and exit `0`, where the
first two previously printed an argument error and exited `1`. Added
`LongHelpFlagSetsHelpRequested`, `ShortHelpFlagSetsHelpRequested`, and
`HelpFlagTakesPriorityOverAnInvalidFlag`. Temporarily made the scan
recognize only `"--help"`, not `"-h"` — confirmed exactly
`ShortHelpFlagSetsHelpRequested` failed (nothing else), restored it,
confirmed the full suite (114 tests) passes again.

## Phase 23: a missing display crashed `palim` instead of exiting cleanly

Found checking `palim` against the same class of gap `focus-monitor`
(the project this account also maintains) had just turned up: its
`main.cpp` calls `cv::namedWindow()`/`cv::imshow()` for its own live
preview, and OpenCV's Qt/GTK highgui backend doesn't fail that call
with a catchable C++ exception when no display is reachable. Confirmed
directly (same OpenCV/Qt installation, same call): it aborts the whole
process via Qt's `qFatal`, which calls `abort()` -- a signal, not a
throw, so nothing in `main()` could ever have caught it.

Worse here than a bare crash would be on its own: `main()` already
does real cleanup work on shutdown (Phase 10's `ShutdownFlag`, joining
the capture/processing/storage threads so a commit never gets caught
mid-write) -- but `abort()` bypasses all of that, the same
"half-written commit" failure mode Phase 10 exists to prevent, just
reached through the one path (the main thread's own GUI loop) nothing
had guarded against it. Concretely reachable, not theoretical: a
systemd unit with no desktop session attached, or a plain SSH session
without `-X`, with a real camera plugged in (so the existing
`Camera::isOpened()` check doesn't stop it first) -- that combination
starts the camera and all three worker threads, streams real frames,
and only then crashes out from under them the instant the main thread
reaches `cv::namedWindow()`.

Added `src/display_availability.hpp`'s `displayAvailable()`, a pure
function over `DISPLAY`/`WAYLAND_DISPLAY`'s values (not read
internally, so the decision itself stays unit-testable without
touching the real environment) -- the exact same fix, independently
applied, as `focus-monitor`'s own `DisplayAvailable()`. `main()` checks
it right after the `--help` path (which still works with no display,
since it returns before this) and before `Camera` is ever constructed,
printing a clear error and returning `1` instead of touching the
camera or starting any thread.

Added `tests/test_display_availability.cpp` (5 cases, including that
an explicitly-empty `DISPLAY=""` counts as unset, same as the pointer
being null). Verified against the real binary: with
`DISPLAY`/`WAYLAND_DISPLAY` both unset, `palim` now prints the new
error and exits `1` instead of reaching the camera at all; `--help`
is unaffected (it returns before this check); setting `DISPLAY` falls
through to the existing camera-open failure exactly as before (no real
`/dev/video0` in this environment). Verified the new test actually
exercises the OR logic: temporarily dropped the `WAYLAND_DISPLAY` half
of the check, confirmed exactly `WaylandDisplayAloneIsAvailable` failed
(nothing else, including `BothSetIsAvailable`, which still has
`DISPLAY` set), restored it, confirmed the full suite (119 tests)
passes again.

## Phase 24: no CI at all

Found stepping back from individual bugs to ask what, structurally,
was still missing: `focus-monitor` (the project this account also
maintains) has had a GitHub Actions workflow building and running its
test suite on every push since early on. `palim` never did -- no
`.github/workflows/` directory existed at all. Every one of the 23
phases before this one landed with only this account's own local
`ctest` run as evidence, never anything that would catch a regression
introduced by a change this account *didn't* make, or verify that the
repository still builds cleanly from a fresh checkout rather than
whatever incrementally-updated `build/` directory happened to be
lying around locally.

Added `.github/workflows/build-and-test.yml`: on push or pull request,
installs the exact dependency line from this README's own Building
section (`libopencv-dev cmake build-essential linux-libc-dev
libgtest-dev`), then configures, builds, and runs `ctest
--output-on-failure` -- the same three commands this README already
tells a human contributor to run, now run automatically instead of
only on trust.

Verified: ran the exact same sequence locally against a from-scratch
checkout (`rm -rf build` first, so nothing from a prior local build
could paper over a real configure/build failure) -- OpenCV, GTest,
and V4L2 headers resolve correctly and the full suite (119 tests)
passes clean, the same outcome CI itself will see on its own fresh
runner (which, like this verification, has no camera -- exactly why
`Camera::grab()` against real hardware stays out of scope for both).

## Phase 25: `main()` always returned `0`, even when the camera disconnected

Found checking `palim` against its own README's framing of itself: a
tool meant to be "left running unattended" (the exact phrase Phase 17's
`--max-commits` exists to support), with a capture thread that already
detects the camera disconnecting mid-session and shuts everything down
cleanly for it. But `main()`'s own final `return 0;` ran no matter
*why* the shutdown happened -- Esc, `Ctrl-C`/`kill`, or the camera
falling off the bus all returned the identical exit code. A supervisor
(a systemd unit with `Restart=on-failure`, a watch script) had no way
to tell "the user stopped this on purpose" apart from "the camera
disconnected, this is worth restarting for" from the exit code alone --
exactly the distinction `focus-monitor`'s own
`kCameraDisconnectedExitCode` already makes, just never applied here,
even though palim's own multi-threaded capture loop already has a
clearer "the camera is gone" signal than focus-monitor's single-
threaded one does (`ConsecutiveFailureGuard::recordFailure()` returning
`true`).

Added a `kCameraDisconnectedExitCode` constant (`2`, matching
`focus-monitor`'s own) and a `std::atomic<bool> cameraDisconnected`
local to `main()`, captured by reference into the capture thread's
lambda alongside everything else it already captures by reference.
Set right next to the existing `ShutdownFlag::instance().request()`
call in the "giving up" branch -- same condition, same place, just
also recording *why* this time. `main()`'s final `return` now checks
it after all three threads have joined (itself a happens-before edge,
though the flag is still `std::atomic`, not a plain `bool`, for the
same reason `ShutdownFlag` is: making the cross-thread intent
unambiguous rather than relying on a guarantee that's easy to reason
past later) and returns `kCameraDisconnectedExitCode` instead of `0`
only when it fired.

Verified: confirmed the two paths this change could have broken are
unaffected -- `palim --help` still prints usage and exits `0`; a
bare `palim` with no display still prints the existing error and
exits `1` (both return before the camera is ever opened, let alone
before the new flag is ever touched). Full suite (119 tests)
unaffected, since this logic lives only in `main.cpp`, not
`palim_lib`. The actual camera-disconnected path itself can't be
exercised live in this headless container (no real camera to
disconnect) -- the same category of thing already acknowledged
elsewhere in this project as untestable here (`Camera::grab()`
against real hardware); verified by careful code review instead:
`ConsecutiveFailureGuard`'s own threshold logic and `ShutdownFlag`'s
cross-thread signaling are both already unit-tested in isolation, and
the new flag's capture-by-reference lifetime is sound because `main()`
never returns until after `captureThread.join()`, well past every
point the capture thread could still be writing to it.

## Explicitly deferred (not forgotten — see spec for full detail)

- Physical-side "what" something changed, beyond *where* it changed (object recognition/classification — see Phase 8 above for the "where" step already done) (spec section 4)
- "Possible cause" debugging suggestions (spec section 4)

## Phase 26: `ChangeDetector`'s own two knobs were never exposed as flags

Found rereading `Config`'s own doc comment fresh: "every field here was
previously a hardcoded constant or literal in `main.cpp`, even though
the classes that consume them (`Camera`, `StateMachine`,
`SnapshotWriter`) already accepted these as constructor arguments --
`main.cpp` just never exposed a way to set them without recompiling."
That comment's own list left out a fourth class matching exactly the
same description: `ChangeDetector`'s constructor has taken
`pixelDiffThreshold` (minimum per-pixel intensity difference to count
as "changed") and `blurKernelSize` (the Gaussian blur applied before
that comparison, to filter sensor noise) since Phase 1 -- but
`main.cpp`'s processing thread has always constructed it with
`palim::ChangeDetector detector;`, bare defaults, no way to retune
either one for a noisier camera or a dimmer room without recompiling.
Every other per-frame tuning knob this project has (`--threshold`,
`--stable-ms`, resolution) already went through exactly this
Config-and-flag treatment; these two never did.

Added `Config::pixelDiffThreshold` (default `25`) and
`Config::blurKernelSize` (default `21`), plus `--pixel-diff-threshold`
and `--blur-kernel-size` flags parsed the same way every other numeric
flag already is. `--pixel-diff-threshold` is validated to `[0, 254]`,
not just non-negative: an 8-bit `absdiff` can never exceed `255`, so a
threshold of `255` or above would make `cv::threshold`'s `src > thresh`
never hold, silently disabling change detection instead of failing
loudly at the CLI boundary the way an out-of-range value for every
other flag already does. `--blur-kernel-size` is validated to be a
positive odd integer -- `cv::GaussianBlur` requires exactly that (or a
kernel size of `0`, not applicable here since `ChangeDetector` always
passes sigma `0` too), so anything else would otherwise surface as an
uncaught `cv::Exception` assertion failure out of
`ChangeDetector::preprocess` the first time a frame is processed,
rather than a clear error before the camera is ever opened. The
processing thread now constructs `ChangeDetector` from both config
values instead of the bare default constructor.

Verified against the real binary: `palim --help` lists both new flags;
`palim --pixel-diff-threshold 255` and `palim --blur-kernel-size 10`
both print a clear argument error and exit `1`, instead of either
silently doing nothing (the first case) or crashing well past argument
parsing (the second, had it not been validated at all). Added
`OverridesPixelDiffThreshold`, `ZeroPixelDiffThresholdIsAccepted`,
`PixelDiffThresholdOutOfRangeThrows`, `OverridesBlurKernelSize`, and
`NonPositiveOrEvenBlurKernelSizeThrows`. Verified the odd-kernel-size
check is actually load-bearing, not just plausible-looking: temporarily
replaced it with `if (false)`, confirmed exactly
`NonPositiveOrEvenBlurKernelSizeThrows` failed (nothing else), restored
it, confirmed the full suite (124 tests) passes again.

## Phase 27: a commit that failed to write still reported `COMMIT saved`

Found reading `SnapshotWriter::write()` fresh: it returned `void`, and
every `cv::imwrite()`/`std::ofstream` call inside it went unchecked.
`cv::imwrite()` doesn't throw on an ordinary I/O failure (a full disk,
a permissions problem, a missing directory) -- it returns `false`
silently, the same shape of thing `AppendSessionSummary` in this
account's other project (`focus-monitor`) already exists to handle for
its own file write. Here, nothing checked that return value at all, so
a failed write and a successful one looked identical to every caller:
`main.cpp`'s storage thread printed `COMMIT saved` either way, `nextId_`
still advanced, and `pruneOldCommits()` still ran -- a commit could
silently vanish (no images, no metadata, on disk) while the terminal
output claimed otherwise, with no way for anyone watching the log to
tell. Concretely reachable the way this project is actually meant to
run (`README.md`'s own framing: "a tool that watches a desk ... left
running unattended"): the one scenario a long-running, unattended
capture tool is most exposed to over days or weeks is exactly "ran out
of disk" or "permissions changed under it."

Worse, `std::filesystem::create_directories()` -- called unconditionally
at the top of `write()`, before any `cv::imwrite()` -- doesn't share
that silent-`false` behavior: reproduced directly that it instead
*throws* `std::filesystem::filesystem_error` when the path it needs to
become a directory already exists as something else, e.g. a stray
regular file sitting at the exact `commits/NNN` path a future commit ID
needs. That exception had nothing catching it anywhere between
`write()` and the storage thread's own lambda in `main.cpp`, so it
would have propagated out and terminated the whole process mid-session
-- taking the still-running capture and processing threads' own
in-flight work down with it, over what should only ever have cost this
one commit.

Changed `SnapshotWriter::write()` to return `bool`: `true` only if
`before.jpg`, `after.jpg`, `after_annotated.jpg` (when a changed region
exists), and `metadata.json` all wrote successfully. Wrapped the whole
body in `try`/`catch (const std::exception&)` so a thrown
`filesystem_error` (or any other exception from this path) is caught,
reported once on `stderr` with the specific commit directory and
reason, and turned into a `false` return instead of crashing -- the
same best-effort philosophy this project already applies to
`Camera::getControl()` and `GitInfo`'s own git-subprocess failures,
just not yet applied to the one place actually writing the commit a
whole session exists to produce. Pulled the metadata-writing code out
into a new private `writeMetadata()` so its own `ofstream` could be
checked (`static_cast<bool>(meta)`) and folded into the same overall
`ok` the two `cv::imwrite()` calls already feed. `main.cpp`'s storage
thread now only prints `COMMIT saved` when `write()` returns `true`;
`write()` itself already warns on `stderr` with the specifics, so
there's nothing more for the caller to add on failure, and the storage
thread keeps running either way -- one bad commit no longer threatens
any of the others.

Added `WriteReturnsFalseInsteadOfCrashingWhenTheCommitDirectoryCannotBeCreated`:
blocks the next commit ID with a regular file (reproducing the
`create_directories` throw case directly, the same way the real bug was
first confirmed with a standalone repro program) and checks `write()`
returns `false` rather than crashing, that no half-written artifacts
land anywhere claiming to be that commit, and that the writer is still
usable for the next commit afterward. Verified this is genuinely
load-bearing: temporarily reverted the `try`/`catch` back to the
original unguarded code (keeping the `bool` return for compile parity)
and confirmed the test failed with an uncaught `filesystem_error`
propagating out of the test body -- exactly the crash this change
exists to prevent -- while every other `SnapshotWriterTest` case stayed
green. Restored the real fix, rebuilt, and confirmed the full suite
(125 tests) passes again.

## Phase 28: `palim-timeline` had no `--help` -- Phase 22 only ever reached `palim` itself

Found rereading `timeline_main.cpp` fresh against the same question
Phase 22 already asked of `palim`'s own `main.cpp`: this project ships
*two* executables built from user-facing argv (`palim` and
`palim-timeline`), and Phase 22's `--help`/`-h` fix only ever touched
the first. `timeline_main.cpp` had no argument parsing at all beyond
"`argv[1]`, if present, is the commits directory" -- so `palim-timeline
--help` silently treated `--help` as that directory and printed `No
commits found in "--help"`, not any actual help text. Confirmed
directly against the real binary before changing anything.

Added `TimelineArgs` (`helpRequested`, `commitsDir`, defaulting to
`"commits"` the same way `--output-dir` already does for `palim`
itself) and `parseTimelineArgs()` to `timeline_format.hpp`/`.cpp` --
the same file every other piece of `palim-timeline`-specific logic
already lives in, for the same reason its own top comment already
gives (`timeline_main.cpp` has `int main()`, so nothing in it links
into a test binary without a duplicate-symbol clash). `--help`/`-h` is
scanned for first, before the positional argument is even looked at --
the identical priority Phase 22 gave `palim`'s own `--help` -- and any
further positional arguments beyond the first stay silently ignored,
matching this project's pre-existing behavior (only `argv[1]` was ever
read). `timeline_main.cpp` now calls `parseTimelineArgs()` and checks
`helpRequested` before ever touching `listCommits()`, printing usage to
stdout with exit `0` via a small `printUsage()`, the same shape as
`palim`'s own help path.

Verified against the real binary: `palim-timeline --help` and
`palim-timeline -h` now print `Usage: palim-timeline [commits_dir]
[--help]` and exit `0`, where before they printed the wrong "no
commits found" message; a real commits directory argument is
unaffected. Added `DefaultsToCommitsDirWithNoArguments`,
`PositionalArgumentOverridesTheCommitsDir`,
`LongHelpFlagSetsHelpRequested`, `ShortHelpFlagSetsHelpRequested`,
`HelpFlagIsNotMistakenForACommitsDirectory`, and
`HelpFlagTakesPriorityOverAPositionalArgument` to
`tests/test_timeline_format.cpp`. Verified these are load-bearing:
temporarily replaced the `--help`/`-h` comparison with `if (false)`,
rebuilt, and confirmed exactly the four help-related tests failed
(`DefaultsToCommitsDirWithNoArguments` and
`PositionalArgumentOverridesTheCommitsDir` stayed green, since neither
exercises the help path) while the rest of the suite was unaffected;
restored the real check, rebuilt, and confirmed the full suite
(131 tests) passes again.

## Phase 29: `listCommits`'s own sort comparator wasn't a strict weak ordering

Found while reviewing `commit_metadata.cpp`'s `listCommits` fresh: the
comment above its `std::sort` call claimed falling back to plain path
comparison "keeps the sort a strict ordering" whenever either side of
a comparison lacked a parsed numeric ID -- but that's not true once a
non-numeric directory (a stray entry, exactly the kind this same
comment already anticipated: a backup directory, anything a human
created in `commits/`) sits alongside IDs that have crossed a
digit-width boundary (the exact scenario `ReturnsCommitsInNumericOrder
AcrossDifferentIdWidths`, added for Phase 21's `cdddd99`, already
establishes happens on a long-running session). With `"999"`,
`"1000"`, and a stray `"5_quarantine"`: the old comparator said
`"1000" < "5_quarantine"` and `"5_quarantine" < "999"` (both fall back
to raw path comparison, and `'1' < '5' < '9'`), which by transitivity
would require `"1000" < "999"` -- but the numeric branch, correctly,
says 1000 is *not* less than 999. That's a strict-weak-ordering
contract violation, `std::sort`'s own documented requirement for its
comparator, not just an unlucky-looking edge case.

Reproduced directly with a standalone repro before touching anything:
confirmed `comp(1000, 5_quarantine)` and `comp(5_quarantine, 999)` both
true while `comp(1000, 999)` is false under the old logic -- the exact
A<B, B<C, but not A<C shape a valid comparator must never produce.
Also checked whether this visibly misorders `listCommits`'s actual
output with the current `libstdc++`: across dozens of shuffled inputs
up to 2000+ entries, `std::sort`'s result still happened to satisfy
`std::is_sorted` under the same (broken) comparator every time --
undefined behavior that doesn't reliably manifest as visibly wrong
output isn't the same as safe, the same lesson the project's INT_MIN/
stack-alignment-class fixes in its sibling projects already established,
just encountered here instead of there.

Pulled the comparator out of `listCommits`'s own lambda into a named,
exported `commitDirectoryLessThan(a, b)` (declared in
`commit_metadata.hpp`, matching `parseTimelineArgs`/`BuildNotifySendCommand`-
style "pull the one part that actually needs checking into its own
function" precedent elsewhere in this account's projects) specifically
so its pairwise contract can be tested directly, independent of
whatever comparison pattern `std::sort` happens to use for a given
input size. Fixed by always ranking a directory with a parsed numeric
ID before one without, rather than only privileging the numeric
comparison when *both* sides have one: group membership (has an ID or
not) is now itself consistent and totally ordered, so there's no path
left for an A<B<C<A-style cycle between a numeric and a non-numeric
entry. Also corrected `listCommits`'s own header comment, which still
described the pre-Phase-21 "zero-padded, so lexicographic == numeric
order" behavior rather than the numeric-ID sort Phase 21 actually put
in place.

Added `CommitDirectoryLessThan.NumericIdSortsBeforeNonNumericEntry
EvenAcrossDigitWidths` (the direct, deterministic pairwise check --
this is the one that's actually load-bearing) and `ListCommits.NumericIds
SortBeforeAStrayNonNumericEntryEvenAcrossDigitWidths` (the end-to-end
companion, pinning down the real user-visible order). Verified the
first is genuinely load-bearing exactly the way this project always
does: temporarily reverted `commitDirectoryLessThan` to the old
fallback-to-path logic, rebuilt, and confirmed the pairwise test failed
on exactly the predicted assertion while the end-to-end `ListCommits`
test -- as the investigation above already found -- stayed green even
with the real bug present, confirming that test alone would *not* have
caught this; restored the fix, rebuilt, and confirmed the full suite
(133 tests) passes again.

## Phase 30: a failed buffer requeue was logged and ignored, not treated as a failure

Found reading `Camera::grab()` fresh. Its capture cycle is
`VIDIOC_DQBUF` (driver hands over a filled buffer) -> convert YUYV to
BGR (copying into a `cv::Mat` this code owns) -> `VIDIOC_QBUF` (hand
the now-empty buffer back to the driver so it can be filled again).
`VIDIOC_DQBUF` failing already returns `std::nullopt`, correctly
feeding `main.cpp`'s `ConsecutiveFailureGuard` the same way a
disconnected camera does. `VIDIOC_QBUF` failing -- the requeue step --
only ever logged the error and still returned the successfully
captured frame.

That's not a cosmetic difference between the two failure paths: once
`VIDIOC_QBUF` fails, that specific buffer is gone from the driver's
queue for good -- nothing later ever retries requeuing it, and
`grab()` has no path that would. Returning the frame anyway reports
success to `main.cpp`, which calls `recordSuccess()` on the exact same
`ConsecutiveFailureGuard` a real disconnect needs 30 consecutive
*failures* to trip -- so a requeue failure not only goes unreported,
it actively resets whatever failure count had been accumulating from
anything else. Repeated failures of this kind permanently shrink the
usable buffer pool (`requestBuffers()` asks for 4, but the driver can
grant as few as 2) with every one of them silently "succeeding." Once
every buffer has been lost this way, the next `VIDIOC_DQBUF` has
nothing left to dequeue and blocks forever -- a silent, unrecoverable
hang that `ConsecutiveFailureGuard` could never catch, since `grab()`
would never return again to update it either way.

Changed the `VIDIOC_QBUF` failure branch to return `std::nullopt`
instead of the already-captured `bgr`, discarding that one frame --
the same outcome `VIDIOC_DQBUF` failing already produces, and exactly
what feeds a requeue failure into the same consecutive-failure
counting a real disconnect relies on. A driver that keeps failing to
requeue is now caught and reported as a disconnect after enough
consecutive failures, the same way a truly unplugged camera already
is, instead of degrading silently toward an unexplained hang.

`Camera::grab()` against a real V4L2 device can't be exercised in this
headless container (no `/dev/video0`, as `tests/test_camera.cpp`'s own
comment already states) -- this environment has no way to make a real
`VIDIOC_QBUF` call fail on demand to add a direct regression test for
it, the same documented limitation this file's own `## Testing` story
already carries for every other `Camera::grab()` behavior. Verified by
code review instead: traced every call site between the old `return
bgr;` (now gated behind the new `return std::nullopt;`) and confirmed
`main.cpp`'s capture-thread lambda already treats any `nullopt` from
`grab()` identically regardless of *why* it happened (DQBUF failing or
QBUF failing) -- `recordFailure()`, then either retry after a short
delay or give up and report a disconnect once the threshold is hit --
so this change needed no new branch anywhere outside `Camera` itself.
Rebuilt and confirmed the full suite (133 tests) still passes
unaffected, as expected for a change confined entirely to a code path
this suite has never been able to exercise.

## Phase 31: the timeline's own "what changed?" diff silently skipped two camera settings

Phase 6's own description of `palim-timeline` promises it "calls out
any camera setting that changed (exposure, gain, white balance)"
between consecutive commits. `CommitMetadata` has carried
`exposureAuto`/`whiteBalanceAuto` (V4L2's own auto-exposure/auto-white-
balance mode controls, read via `VIDIOC_G_CTRL` and round-tripped
through `metadata.json` the same as every other camera setting) since
Phase 5 -- but `timeline_main.cpp`'s between-commit diff block only
ever called `formatOptionalIntChange()` for `exposureAbsolute`, `gain`,
and `whiteBalanceTemperature`. A camera switching from auto-exposure to
manual (or back) between two commits -- a genuinely meaningful,
deliberate-feeling change, arguably more so than the absolute exposure
value drifting on its own while still in auto mode -- was silently
never reported, even though the exact same diffing mechanism already
existed and was already wired up for the other three fields.

Added two more `formatOptionalIntChange()` calls in `timeline_main.cpp`
-- `exposure_auto` and `white_balance_auto` -- right alongside the
existing three, using the exact same function with no changes needed
to `timeline_format.hpp/.cpp` itself: `formatOptionalIntChange` was
already fully generic over any `std::optional<int>` pair, this was
purely a case of two fields nobody had ever wired into the call site
that uses it.

`timeline_main.cpp` has `int main()`, so nothing in it links into the
test binary (the same reason `timeline_format.cpp` was split out of it
in the first place) -- verified instead the same way Phase 6's own
"Verified" paragraph did, against the real binary: built a synthetic
two-commit `commits/` directory by hand with `exposure_auto`/
`white_balance_auto` flipping from `1` to `0` between them, confirmed
the unmodified binary printed an empty `|` -> `v` block with the change
silently missing, then rebuilt with the fix and confirmed it now
prints `exposure_auto: 1 -> 0` and `white_balance_auto: 1 -> 0`.
Confirmed the full suite (133 tests) is unaffected either way, as
expected for a change confined to `timeline_main.cpp`'s own wiring.

## Phase 32: dirty-file paths with a quote or backslash were recorded garbled

`parseDirtyFiles` takes each `git status --porcelain` line, skips its
2-character status code and the space after it, and keeps the rest as
the dirty file's path -- reasonable, since an ordinary path is exactly
that substring. But `git status --porcelain` doesn't emit a path
verbatim once it contains a character core considers "unusual": a
literal double quote or backslash, a control character, or (since
`core.quotePath` defaults to true) any non-ASCII byte. Instead it wraps
the *entire* path in double quotes and C-escapes every such byte
inside it. Confirmed directly against a real repository, not assumed:
a file named `weird"quote.txt` shows up in porcelain output as the
18-character literal `"weird\"quote.txt"`, and a file named
`back\slash.txt` as `"back\\slash.txt"` -- `parseDirtyFiles` was taking
that literal quoted-and-escaped text as the path, rather than the real
15- and 14-character filenames. Every consumer downstream --
`metadata.json`'s `dirty_files` array, `palim-timeline`'s "newly dirty
files" diff -- inherited the same garbled text, for any dirty file
whose name happened to be unusual enough to trigger this.

Added `unquoteGitPath(path)`: returns `path` unchanged unless it's
quoted over its *entire* length (the common case -- an ordinary
filename never gets this treatment at all), and otherwise reverses
core's own escaping -- `\"`, `\\`, the usual single-letter C escapes
(`\n`, `\t`, ...), and `\NNN` three-digit octal escapes for a raw
non-ASCII byte. Exported from `git_info.hpp` (not kept file-local, the
way `rstrip`/`runCommand` are) specifically so this escaping logic --
the one part of parsing a dirty-file line that can silently mangle a
real filename, not just mis-split a line -- is directly testable on
its own, independent of whatever a given porcelain line's shape
happens to be. Wired into `parseDirtyFiles` around the existing
`rstrip(line.substr(3))` call. Deliberately left a rename line's
`"<old> -> <new>"` combined string (see that function's own comment)
unresolved when only one side is quoted -- unquoting half of a
still-combined string would mean guessing which half changed, not
something this fix tries to solve.

Verified the real quoting/escaping shape directly against a real git
repository before writing anything: created files named
`weird"quote.txt` and `back\slash.txt`, ran `git status --porcelain`,
and confirmed byte-for-byte what came back (`cat -A`) before using that
exact text as the test fixtures below, rather than guessing at git's
escaping format. Added `ParseDirtyFiles.UnquotesAPathContainingADouble
Quote`/`UnquotesAPathContainingABackslash` (the end-to-end case, using
that captured real output), `ParseDirtyFiles.LeavesAnUnquotedPathAlone`
(the common case must stay untouched), and four direct
`UnquoteGitPath.*` tests covering the unquoted-passthrough case, both
escape kinds together, a genuine octal byte escape (`\303\251`, UTF-8
for é), and the "quoted at only one end" rename case staying
unresolved on purpose. Verified the fix is actually load-bearing:
reverted `parseDirtyFiles` to call `rstrip(line.substr(3))` directly
again (skipping the new unquoting call), rebuilt, and confirmed exactly
the two end-to-end tests failed with the predicted garbled output while
all four direct `UnquoteGitPath` tests -- which exercise the function
itself, not its wiring -- stayed green, confirming those two were the
only ones actually testing the regression this fix closes. Restored
the fix, rebuilt, and confirmed the full suite (140 tests) passes
again.

## Phase 33: no compiler warnings enabled at all, and what enabling them found

Unlike this account's other two from-scratch projects -- os-practice's
Makefile has carried `-Wall -Wextra` since its very first commit;
focus-monitor's CMakeLists.txt gained the same, then `-Werror`, then
`-Wshadow -Wpedantic`, across several days once the gap was noticed --
`palim`'s CMakeLists.txt never turned on any compiler warnings at all.
Added `add_compile_options(-Wall -Wextra -Wshadow -Wpedantic -Werror)`
globally, before any `add_library`/`add_executable`, so `palim_lib`,
both real executables, and the test binary all get it, not just one
target. `target_include_directories(palim_lib PUBLIC ...)` previously
mixed this project's own `src/` directory with `${OpenCV_INCLUDE_DIRS}`
in one call -- split into two, with OpenCV's own include directories
now passed with the `SYSTEM` keyword, matching focus-monitor's own
precedent: `-Werror` means any warning now fails the build, including
one originating inside a header this project doesn't own, and CI's own
build step installs whatever `libopencv-dev` the runner's current
package index has rather than a version pinned anywhere in this repo.
`SYSTEM` suppresses warnings from inside OpenCV's own headers
regardless of these flags while leaving all of them fully enforced
against this project's own code (confirmed directly in the generated
`flags.make`: `-isystem /usr/include/opencv4`, not `-I`).

Unlike focus-monitor's own history (where the codebase was already
clean when the flags were added), enabling these on `palim` found two
real things, both fixed in this same change rather than left as
follow-up work:

**`FrameHistory`'s own internal storage silently carried a dead field.**
`FrameHistory::push()` built its ring-buffer entries as
`FrameSample{timestamp, frame.clone()}` -- but `FrameSample` (defined
right above it, and also used as the capture-to-processing queue's own
message type in `main.cpp`) has *three* fields: `timestamp`, `frame`,
and `settings`. The third was always left default-constructed inside
`FrameHistory`'s buffer, and nothing anywhere ever read a buffered
frame's `settings` back out -- `bestFrameInRange()` only ever returns
`std::optional<cv::Mat>`, never the settings alongside it.
`-Wmissing-field-initializers` (part of `-Wextra`) flagged this as a
real aggregate-initialization call missing a member, not a style nit:
reusing a type built for a different role (carrying camera settings
from the capture thread to the eventual commit) inside a role that
never needed that field meant every buffered frame was copying a
`CameraSettings` (five `std::optional<int>`s) for no reason, and -- had
anyone ever actually started reading `.settings` off a buffered frame
later, expecting it to reflect *that* frame's own capture-time
settings -- they'd have silently gotten default-constructed nulls
instead. Fixed by giving `FrameHistory` its own private nested
`BufferedFrame` struct (`timestamp` + `frame` only) instead of reusing
`FrameSample`, and switching `RingBuffer<FrameSample> buffer_` to
`RingBuffer<BufferedFrame> buffer_` accordingly. `FrameSample` itself
is untouched -- `main.cpp`'s own use of it (where `settings` is
genuinely meaningful) didn't need to change at all.

**Three GTest range-for loops bound a reference to a per-iteration
temporary.** `tests/test_commit_metadata.cpp` had
`for (const std::string& id : {"003", "001", "002"})` (and two more
like it) -- the braced list is `std::initializer_list<const char*>`,
so binding a `const std::string&` to each element implicitly
constructs a temporary `std::string` per iteration.
`-Wrange-loop-construct` (part of `-Wextra`) flags this: while the
temporary's lifetime is correctly extended to match the reference's
scope here (not a dangling-reference bug in this specific case), it's
exactly the kind of implicit-conversion-inside-a-range-for pattern
that *can* dangle under slightly different code shapes, and GCC's own
suggested fix -- binding by value instead -- is strictly clearer about
what's actually happening. Changed all three loop variables from
`const std::string&` to `const std::string`.

Verified the whole codebase (`palim_lib`, `palim`, `palim-timeline`,
`palim_tests`) builds clean from scratch under the new flags, and that
the `FrameHistory` fix is genuinely load-bearing: temporarily reverted
`frame_history.hpp`/`.cpp` back to reusing `FrameSample` (restoring the
exact original missing-field-initializer call), rebuilt, and confirmed
the identical "`missing initializer for member
'palim::FrameSample::settings'`" error reappeared in exactly that one
file. Restored the fix, rebuilt, and confirmed the full suite (140
tests) passes again.
