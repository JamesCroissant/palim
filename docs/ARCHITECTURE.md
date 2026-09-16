# palim — Phase 1 MVP Architecture

Status: Phase 1 (Steps 1–5), Phase 1.5 (ring buffer + best-frame
selection), Phase 2 (raw V4L2 capture), Phase 3 (multithreading), and
Phase 4 (Git integration), and Phase 5 (V4L2 control metadata) are
implemented. This document exists to
agree on the
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
│   ├── main.cpp              # wires everything together, owns the loop
│   ├── camera.hpp/.cpp       # Camera: wraps cv::VideoCapture
│   ├── change_detector.hpp/.cpp   # ChangeDetector: frame diff → changed %
│   ├── state_machine.hpp/.cpp     # StateMachine: STABLE/CHANGING/WAIT_FOR_STABLE
│   ├── snapshot_writer.hpp/.cpp   # SnapshotWriter: before/after jpg + metadata.json
│   ├── frame_quality.hpp/.cpp     # computeSharpness(): shared by SnapshotWriter + FrameHistory
│   ├── ring_buffer.hpp            # RingBuffer<T>: generic fixed-capacity circular buffer
│   ├── frame_history.hpp/.cpp     # FrameHistory: recent-frames window + best-frame-in-range
│   └── blocking_queue.hpp         # BlockingQueue<T>: thread-safe producer/consumer queue
└── commits/                  # runtime output, gitignored
    └── 001/
        ├── before.jpg
        ├── after.jpg
        └── metadata.json
```

Flat `src/` on purpose — the file count still doesn't justify
`capture/`/`processing/`/`state/` subfolders. We'll split it out when
multithreading actually adds enough files to need it.

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

## Explicitly deferred (not forgotten — see spec for full detail)

- Timeline UI (section 16)
