# Performance

Every number in this repository comes from one of three places, each reproducible with one
command.

| Source | Command | Measures |
|---|---|---|
| In-app monitor | run the app; see the Performance card | Live frame pacing, sync cost, CPU, RSS |
| Benchmark mode | `reality-view-native --benchmark-seconds 8 [--rate 4000]` | JSON summary after a 1 s warm-up |
| Micro-benchmarks | `./build/dev/bench/rvn_bench` | Hot paths in isolation, median of 7 |

## How frames are measured

`FrameMonitor` connects to `QQuickWindow` signals that fire **on the render thread**, using
`Qt::DirectConnection`:

- `frameSwapped` → time between swaps = the real frame interval the user sees.
- `beforeSynchronizing` → `afterSynchronizing` = how long the **GUI thread is blocked** while
  `updatePaintNode` runs. It is the render-side cost that can steal time from QML and input.

Timings go to the GUI thread through an SPSC queue and are aggregated at 4 Hz into p50, p95,
p99, max and jank.

**Jank** is a frame longer than 1.5× the display's refresh interval, read from
`QScreen::refreshRate()`. An early version hard-coded 60 Hz, which judged a 120 Hz ProMotion
display against the wrong deadline. Averages hide stutter, so the monitor reports tail
percentiles, and the frame-time chart draws the vsync budget as a guide line.

## Results

Apple M4, macOS, 120 Hz display, Release build, Metal backend.

```
$ reality-view-native --benchmark-seconds 8
fps 113.5 | p50 8.34 ms | p95 16.4 ms | p99 17.2 ms | sync 0.19 ms | RSS 146 MB

$ reality-view-native --benchmark-seconds 5 --rate 4000
fps 110.8 | p50 8.35 ms | p95 16.7 ms | p99 25.0 ms | sync 0.09 ms | RSS 146 MB
```

### Reading these numbers

- **Telemetry rate doesn't affect the frame rate.** Going 16× from 240 Hz to 4 kHz leaves fps
  and p50 unchanged, because per-frame work is constant.
- **Our render-side cost is about 0.2 ms of an 8.3 ms budget.** The rest of the frame is Qt
  Quick's own work (QML animations, bindings, batching, Metal submission) and waiting for vsync.
- **Missed frames at 120 Hz.** About 5% of frames take two vsync intervals (p95 ≈ 16.6 ms). The
  sync phase is 0.2 ms and the drain is about 2 µs, so the next suspects are QML-side.
  Candidates are the stale-banner and HUD animations, text layout of about 40 `Text` items
  re-bound every frame, and macOS compositor scheduling. The next step is an Instruments
  *Animation Hitches* trace plus `QSG_RENDER_TIMING=1`, which splits sync, render and swap
  times. Then re-measure with the right-hand panel hidden to isolate QML cost. This is listed as
  open work rather than claimed as solved.
- **Startup is excluded.** Benchmark mode resets statistics after 1 s, so shader compilation and
  first uploads don't pollute steady-state numbers.

### Micro-benchmarks

| Benchmark | Time | Why it matters |
|---|---:|---|
| SPSC ring buffer, 2 threads | 159 M items/s | 24× `std::mutex` + `std::deque` (6.7 M/s) |
| `DriveSimulator::step` | 444 ns | 0.1% of one core at 4 kHz |
| `Road::project`, hinted | 226 ns | vs 38 µs brute force on a 20 km road (170×) |
| Consumer drain, 17 samples | 299 ns | one frame of 1 kHz telemetry |
| `buildRoadMesh`, 800 m | 101 µs | 26k vertices, 309 KB; runs every 150 m (about every 5 s at 65 mph) |
| `buildTrailMesh`, 2048 points | 36 µs | largest per-frame mesh; see "What I would do next" in ARCHITECTURE.md |
| Traffic + ego meshes | 1.0 µs | per frame |
| `FrameStats::summary` | 0.9 µs | `nth_element`, 4× per second |

`sizeof(TelemetrySample)` is 424 bytes, so the 4096-slot queue is 1.7 MB. That is bounded
and allocated once.

## Memory

Memory is bounded by construction. The app has:

- A fixed-capacity sample queue (1.7 MB) and command queue.
- Fixed-capacity `RingHistory` for the trail (2048 points) and speed (600 points).
- Vertex vectors reserved once and reused (`clear()` keeps capacity).
- One road that grows at 32 bytes per metre: about 3.2 MB per 100 km, as two copies, one per
  thread. This is the only growth. Trimming points behind the car is a known follow-up.

RSS stays flat at about 146 MB during long runs; the Performance card shows current and peak.
Most of it is Qt, Metal and font caches. To verify there are no leaks:

```sh
# macOS
leaks --atExit -- ./build/dev/src/app/reality-view-native.app/Contents/MacOS/reality-view-native --benchmark-seconds 30
# Linux
valgrind --tool=massif ./build/release/src/app/reality-view-native --benchmark-seconds 30
heaptrack ./build/release/src/app/reality-view-native --benchmark-seconds 30
# Core library, any platform
cmake --preset asan && cmake --build --preset asan && ctest --preset asan   # LeakSanitizer included
```

## CPU profiling

```sh
# macOS: Time Profiler and Animation Hitches
xcrun xctrace record --template 'Time Profiler' --launch -- \
  ./build/dev/src/app/reality-view-native.app/Contents/MacOS/reality-view-native --benchmark-seconds 15

# Linux
perf record -g ./build/release/src/app/reality-view-native --benchmark-seconds 15
perf report --no-children

# Qt's own breakdown of sync, render and swap per frame
QSG_RENDER_TIMING=1 ./reality-view-native
# Scene-graph batching: how many batches and whether meshes merge
QSG_RENDERER_DEBUG=render ./reality-view-native
```

Process CPU runs at about 30–50% of one core at 120 fps on the M4, across all threads. The
telemetry thread uses under 1% at 240 Hz (a 444 ns step plus sleep). The rest is Qt Quick
running at 120 Hz.

## Concurrency verification

```sh
cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan
```

The TSan job in CI runs every core test, including the 2-million-item cross-thread queue test
and the stall, dropout and overflow pipeline tests.
