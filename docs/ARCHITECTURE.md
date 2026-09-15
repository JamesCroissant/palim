# palim — Phase 1 MVP Architecture

Status: proposal, not yet implemented. This document exists to agree on the
shape of the system *before* writing code, per the project's own philosophy
of understanding each layer (camera → kernel → V4L2 → buffer → OpenCV →
processing → event → storage) rather than hiding it behind a library.

## Goal of Phase 1

Prove the smallest possible loop:

> physical state change on the desk → automatically detected → before/after
> frames saved with metadata

No threading, no V4L2 raw ioctl calls, no ring buffer, no best-frame
selection, no Git integration, no GUI. Those are named explicitly in later
phases below so it's clear they're deferred, not forgotten.

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
│   └── snapshot_writer.hpp/.cpp   # SnapshotWriter: before/after jpg + metadata.json
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

- **main.cpp** — the loop: `grab → detect → feed state machine → on commit,
  write`. No class of its own; this is intentional so the data flow stays
  visible in one place instead of buried in a "Pipeline" abstraction we
  don't need yet.

## Data flow (Phase 1)

```
Camera::grab()
      │  cv::Mat frame, timestamp
      ▼
ChangeDetector::compare(lastStableFrame, frame)
      │  double changedPercent
      ▼
StateMachine::update(changedPercent, now, frame)
      │  (usually nothing)
      │  occasionally: CommitEvent{before, after, changeScore, sharpnessScore}
      ▼
SnapshotWriter::write(event)
      │
      ▼
commits/NNN/{before.jpg, after.jpg, metadata.json}
```

Everything after `main`'s loop is synchronous, single-threaded. One frame
in, one decision out, per iteration.

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

Sharpness score for Step 5 uses `cv::Laplacian` variance directly in
`SnapshotWriter` — it's recorded as a number, but *choosing between
candidate frames* (spec section 9) is explicitly deferred to Phase 1.5,
once a ring buffer exists to have candidates from in the first place.

## Explicitly deferred (not forgotten — see spec for full detail)

- Ring buffer of recent frames (section 10)
- Multithreading: capture / processing / storage threads (section 11)
- Raw V4L2 (open/ioctl/mmap) instead of `cv::VideoCapture` (section 12)
- V4L2 control metadata (exposure, gain, white balance) in metadata.json (section 15)
- Git integration (commit hash, dirty files) (section 14)
- Timeline UI (section 16)
