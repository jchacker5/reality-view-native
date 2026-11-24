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
fps 120.0 | p50 8.32 ms | p95 9.4 ms | p99 9.7 ms | sync 0.19 ms | RSS 146 MB   (quiet machine)
fps 113.5 | p50 8.34 ms | p95 16.4 ms | p99 17.2 ms | sync 0.19 ms | RSS 146 MB   (4-core background build)

$ reality-view-native --benchmark-seconds 5 --rate 4000
fps 110.8 | p50 8.35 ms | p95 16.7 ms | p99 25.0 ms | sync 0.09 ms | RSS 146 MB
```

### Reading these numbers

- **Telemetry rate doesn't affect the frame rate.** Going 16× from 240 Hz to 4 kHz leaves fps
  and p50 unchanged, because per-frame work is constant.
- **Our render-side cost is about 0.2 ms of an 8.3 ms budget.** The rest of the frame is Qt
  Quick's own work (QML animations, bindings, batching, Metal submission) and waiting for vsync.
- **Missed frames at 120 Hz** were caused by background machine load, not the app. On a quiet
  machine it matches an empty window. See the investigation below.
- **Startup is excluded.** Benchmark mode resets statistics after 1 s, so shader compilation and
  first uploads don't pollute steady-state numbers.

## Investigation: missed frames at 120 Hz

**Symptom.** About 5–9% of frames took two vsync intervals (p95 ≈ 16.6 ms) on a 120 Hz display.
**Resolved:** machine load, not the app (step 4).

**1. Localize by phase.** `QSG_RENDER_TIMING=1` splits each frame. The GUI thread's sync costs
about 0 ms. Slow frames show the time inside the renderer's *render* phase (10–76 ms spikes),
which on Metal includes waiting for a drawable. Our CPU-side work is ruled out (sync 0.2 ms,
drain about 2 µs).

**2. Bisect the content** (`bench/frame_matrix.sh`, three 8 s runs per configuration):

| Configuration | Missed frames per run |
|---|---|
| Baseline (3D tilt, 4× MSAA, side panel) | 5.6%, 7.9%, 30.3% |
| `--msaa 0` | 4.7%, 7.1%, 13.2% |
| `--view 2d` | 6.4%, 6.7%, 7.2% |
| `--panel off` | 3.8%, 5.2%, 5.4% |
| All three off | 0.4%, 5.8%, 9.0% |

Run-to-run variance is larger than any configuration effect. No content change reliably removes
the misses.

**3. Run a control.** `tools/pacing_probe` is a near-empty Qt Quick window, one rotating
rectangle, measured with the same `FrameStats` code. Interleaved runs (control, then app, four
rounds):

| Round | Control missed | App missed |
|---|---:|---:|
| 1 | 6.3% | 9.3% |
| 2 | (no frames: window never shown, invalid) | 7.4% |
| 3 | 2.8% | 27.7% (outlier) |
| 4 | 4.1% | 5.2% |

**An empty window misses 3–6% of 120 Hz frames on this machine.** During these runs a
TypeScript compiler was using about 4 cores in the background, a realistic developer-machine
load. The app tracks the control within a few points, apart from one outlier.

**4. Re-run on a quiet machine.** With the background compiler finished, the same interleaved
protocol gives:

| Round | Control: fps / p99 / missed | App: fps / p99 / missed |
|---|---|---|
| 1 | 120.0 / 9.49 ms / 0.0% | 119.5 / 10.38 ms / 1.2% |
| 2 | 119.9 / 9.04 ms / 0.1% | 120.0 / 9.66 ms / 0.0% |
| 3 | 120.0 / 9.57 ms / 0.0% | 120.0 / 9.71 ms / 0.1% |

**Conclusion.** The misses came from machine load (compositor and GPU scheduling under
contention), not from this app. On a quiet machine the app holds the full 120 Hz with a p99
under 10.5 ms, the same as an empty window. Under heavy background load both degrade together.
On target hardware, the equivalent safeguard is reserving CPU and GPU budget for the UI process
(priorities, isolation) rather than micro-optimizing content. To reproduce:

```sh
bench/frame_matrix.sh build/dev/src/app/reality-view-native.app/Contents/MacOS/reality-view-native 8 3
build/dev/tools/pacing_probe 8
```

**Side finding: a deadlock in the probe itself.** The first version hung on exit. Its quit
handler held the stats mutex while calling `QCoreApplication::quit()`, which on macOS closes the
window synchronously and waits for the render thread. The render thread was blocked on that
same mutex in `frameSwapped`. The fix: copy the data under the lock, release it, then call into
the framework. This is why the app hands frame timings over through an SPSC queue instead of a
mutex.

### Micro-benchmarks

| Benchmark | Time | Why it matters |
|---|---:|---|
| SPSC ring buffer, 2 threads | 159 M items/s | 24× `std::mutex` + `std::deque` (6.7 M/s) |
| `DriveSimulator::step` | 444 ns | 0.1% of one core at 4 kHz |
| `Road::project`, hinted | 226 ns | vs 38 µs brute force on a 20 km road (170×) |
| Consumer drain, 17 samples | 299 ns | one frame of 1 kHz telemetry |
| `buildRoadMesh`, 800 m | 101 µs | 26k vertices, 309 KB; runs every 150 m (about every 5 s at 65 mph) |
| `buildTrailMesh`, 2048 points | 36 µs | largest per-frame mesh, 0.4% of a 120 Hz frame: deliberately not optimized |
| Traffic + ego meshes | 1.0 µs | per frame |
| `FrameStats::summary` | 0.9 µs | `nth_element`, 4× per second |

`sizeof(TelemetrySample)` is 424 bytes, so the 4096-slot queue is 1.7 MB. That is bounded
and allocated once.

## Memory

Memory is bounded by construction. The app has:

- A fixed-capacity sample queue (1.7 MB) and command queue.
- Fixed-capacity `RingHistory` for the trail (2048 points) and speed (600 points).
- Vertex vectors reserved once and reused (`clear()` keeps capacity).
- Two copies of the road centreline, one per thread. Each generates ahead of the car and trims
  behind it (`Road::trimBefore`, in chunks of 2048 points), so each holds at most about 4,600
  points (about 150 KB) however long the drive. `tst_road` checks the bound over 200 km, and
  `tst_drive_simulator` over a 10-minute drive.

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
