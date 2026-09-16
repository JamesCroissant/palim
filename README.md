# palim

**Git for the physical world.**

A camera tool that watches a desk (or workbench, or breadboard) through a
webcam, automatically detects when something physically changes, and
records the before/after state — the way `git commit` records a change in
code, without you having to stop and write it down yourself.

## Why

Software has `git diff`, `git log`, `git checkout`. Hardware and physical
experimentation don't have an equivalent: "was the USB hub in the loop
when this worked?", "what exposure setting was I using in that test?",
"what did the wiring look like before I changed it?" — normally you'd
have to remember, or stop and take a photo and write a note every time.

`palim` tries to remove that manual step. You just work; the camera
notices when the physical state has meaningfully changed, waits for
things to settle, and saves a record automatically.

The name comes from *palimpsest*: a manuscript whose earlier writing was
scraped away and written over, but never fully disappears — traces of the
previous layer remain visible underneath. That's the idea here: every
physical state, even the ones you've since changed, leaves a trace you
can go back and look at.

## Status

Early and incremental, by design. The current implementation proves the
core loop:

```
physical change on the desk → detected → before/after frames + metadata saved
```

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the full design
rationale and what's implemented.

## How it works

Capture, processing, and storage run on separate threads, connected by
thread-safe queues:

```
[capture thread]              [processing thread]                [storage thread]

Camera (raw V4L2:       FrameHistory (ring buffer of        SnapshotWriter
open/ioctl/mmap)         recent frames)                       (picks the sharpest
      │                        │                                "after" frame,
      ▼                        ▼                                writes before.jpg /
BlockingQueue<FrameSample> ─► ChangeDetector (changed %)         after.jpg /
      │                        │                                metadata.json)
      ▼                        ▼                                     ▲
 displayFrame            StateMachine (STABLE →                      │
 (for the GUI,             CHANGING → WAIT_FOR_STABLE)                │
 [main thread])                │                                     │
                                ▼                                     │
                         BlockingQueue<CommitEvent> ──────────────────┘
```

When the desk holds still for 2 seconds after a detected change, `palim`
writes a numbered commit:

```
commits/
└── 001/
    ├── before.jpg
    ├── after.jpg
    └── metadata.json
```

```json
{
  "timestamp": "2026-09-15T09:22:08",
  "change_score": 42.5,
  "sharpness_score": 1197.52,
  "camera": {
    "width": 1280,
    "height": 720,
    "exposure_auto": 1,
    "exposure_absolute": 220,
    "gain": 32,
    "white_balance_auto": 1,
    "white_balance_temperature": 4600
  },
  "git": {
    "available": true,
    "commit": "a91e32f...",
    "dirty_files": ["src/camera.hpp", "src/main.cpp"]
  }
}
```

The `camera` block comes from `VIDIOC_G_CTRL` at capture time — any
control the device doesn't support reads as `null` rather than failing
the whole commit. The `git` block reflects whatever repo `palim` is run
from (`git` subprocess calls in the current working directory) —
`available: false` if it's not run inside a Git repo, or `git` isn't
installed.

## Building

Requires a C++17 compiler, CMake ≥ 3.16, pthreads, Linux V4L2 headers
(already present on most distros via the kernel headers package), and
OpenCV (`core`, `imgproc`, `highgui`, `imgcodecs`).

```sh
# Debian/Ubuntu
sudo apt-get install libopencv-dev cmake build-essential linux-libc-dev

cmake -S . -B build
cmake --build build
```

## Running

```sh
./build/palim
```

Opens `/dev/video0` directly via V4L2 and shows a live preview. Press
**Esc** to quit. Commits are written to `commits/` in the current
working directory as physical changes are detected.

Developed against a Logitech/Logicool C270 on Linux; any UVC webcam that
supports YUYV capture should work. Linux-only — there's no cross-platform
abstraction here on purpose (see the project's own learning goals in
`docs/ARCHITECTURE.md`).

### Viewing the timeline

```sh
./build/palim-timeline            # reads ./commits by default
./build/palim-timeline path/to/commits
```

Prints each commit's change score, sharpness, camera settings, and Git
state in order, calling out any camera setting that changed since the
previous commit.

## Roadmap

Sections referenced below are from the original project spec; see
`docs/ARCHITECTURE.md` for how they map onto the code.

- [x] Change detection + stable-state commit loop (Phase 1)
- [x] Ring buffer + best-frame selection (Phase 1.5)
- [x] Raw V4L2 capture (open/ioctl/mmap) instead of `cv::VideoCapture` (Phase 2)
- [x] Multithreaded capture / processing / storage pipeline (Phase 3)
- [x] Git integration (pair each physical commit with the current
      `git rev-parse HEAD` and dirty-file list) (Phase 4)
- [x] Richer V4L2 metadata (exposure, gain, white balance) per commit (Phase 5)
- [x] Timeline CLI (`palim-timeline`) (Phase 6)
- [ ] "What changed?" diffing / debugging support across physical + software state

## License

Apache License 2.0 — see [`LICENSE`](LICENSE).
