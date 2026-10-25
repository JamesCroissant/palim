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

## Explicitly deferred (not forgotten — see spec for full detail)

- Physical-side "what" something changed, beyond *where* it changed (object recognition/classification — see Phase 8 above for the "where" step already done) (spec section 4)
- "Possible cause" debugging suggestions (spec section 4)
