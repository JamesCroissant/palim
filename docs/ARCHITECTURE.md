# palim — Phase 1 MVP Architecture

Status: Phase 1 (Steps 1–5) and Phase 1.5 (ring buffer + best-frame
selection) are implemented. This document exists to agree on the
shape of the system *before* writing code, per the project's own philosophy
of understanding each layer (camera → kernel → V4L2 → buffer → OpenCV →
processing → event → storage) rather than hiding it behind a library.

## Goal of Phase 1

Prove the smallest possible loop:

> physical state change on the desk → automatically detected → before/after
> frames saved with metadata

No threading, no V4L2 raw ioctl calls, no Git integration, no GUI. Those
are named explicitly in later phases below so it's clear they're
deferred, not forgotten. Ring buffer + best-frame selection *is* now
implemented (Phase 1.5, see below) since it turned out to be a small,
self-contained addition once Step 5 landed.

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
│   └── frame_history.hpp/.cpp     # FrameHistory: recent-frames window + best-frame-in-range
└── commits/                  # runtime output, gitignored
    └── 001/
        ├── before.jpg
        ├── after.jpg
        └── metadata.json
```

Flat `src/` on purpose for Phase 1 — five small classes don't need
`capture/`, `processing/`, `state/` subfolders yet. We'll split it out when
Phase 2 (multithreading, V4L2) actually adds enough files to justify it.

## Class responsibilities

- **Camera** — owns a `cv::VideoCapture`, opens `/dev/video0`, and exposes
  one method: `grab() -> std::optional<cv::Mat>`. Nothing else. It doesn't
  know about change detection or storage.

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

- **main.cpp** — the loop: `grab → push into FrameHistory → detect → feed
  state machine → on commit, ask FrameHistory for a sharper `after`, then
  write`. No class of its own; this is intentional so the data flow stays
  visible in one place instead of buried in a "Pipeline" abstraction we
  don't need yet.

## Data flow (Phase 1 + 1.5)

```
Camera::grab()
      │  cv::Mat frame, timestamp
      ├──────────────────────────────► FrameHistory::push(frame, timestamp)
      ▼
ChangeDetector::compare(prevFrame, frame)
      │  double changedPercent
      ▼
StateMachine::update(changedPercent, frame, now)
      │  (usually nothing)
      │  occasionally: CommitEvent{before, after, changeScore,
      │                             stableWindowStart, stableWindowEnd}
      ▼
FrameHistory::bestFrameInRange(stableWindowStart, stableWindowEnd)
      │  overwrites event.after if a sharper candidate exists
      ▼
SnapshotWriter::write(event)
      │
      ▼
commits/NNN/{before.jpg, after.jpg, metadata.json}
```

Everything after `main`'s loop is synchronous, single-threaded. One frame
in, one decision out, per iteration.

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
- OpenCV (`core`, `imgproc`, `videoio`, `highgui` — the last only for the
  debug preview window in Step 1)
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

## Explicitly deferred (not forgotten — see spec for full detail)

- Multithreading: capture / processing / storage threads (section 11)
- Raw V4L2 (open/ioctl/mmap) instead of `cv::VideoCapture` (section 12)
- V4L2 control metadata (exposure, gain, white balance) in metadata.json (section 15)
- Git integration (commit hash, dirty files) (section 14)
- Timeline UI (section 16)
