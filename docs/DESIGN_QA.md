# Design Q&A

These are the questions this project is built to answer. Each answer points at the code that
implements it, so it can be demonstrated live as well as explained.

---

### 1. How would you design a vehicle visualization system that renders at 60 FPS?

**Separate data production from rendering, and make per-frame work constant and small.**

- **Three threads, one owner per piece of state.** Telemetry on its own thread, UI logic on the
  GUI thread, drawing on Qt's render thread. They communicate through queues, not shared objects
  ([ARCHITECTURE.md](ARCHITECTURE.md#threads)).
- **Frame-driven consumption.** The UI pulls exactly once per vsync (`FrameAnimation` →
  `VehicleViewModel::tick`). Data doesn't push repaints.
- **Draw into the scene graph directly.** `VehicleSceneItem::updatePaintNode` builds geometry
  nodes. There is no QPainter rasterization and no per-object QML item.
- **Cache what doesn't change.** The 26k-vertex road is rebuilt every 150 m. Between rebuilds,
  motion is one matrix on a `QSGTransformNode`.
- **Budget, then measure.** The frame budget is 16.7 ms at 60 Hz and 8.3 ms at 120 Hz. Our
  render-side sync costs about 0.2 ms, measured live and in `--benchmark-seconds`.
- **Batch-friendly geometry.** There is one vertex format and one material type, so the batch
  renderer merges draw calls.

**Trade-off:** custom scene-graph code is more work than QML `Shape` or `Canvas`, and it's
harder for designers to tweak. For the high-frequency, high-vertex-count part of the screen it
pays for itself. Static chrome stays in QML.

---

### 2. How would you prevent a slow backend from freezing the Qt interface?

**Never do I/O or blocking work on the GUI thread, and never let the GUI thread wait on another
thread.**

- The producer runs on its own `std::jthread`. The GUI thread only performs a **non-blocking
  drain** of an SPSC queue. If nothing has arrived, the drain returns immediately and the frame
  renders with the last known state.
- **Staleness is surfaced rather than hidden.** Each sample carries a steady-clock timestamp.
  When the data age exceeds 250 ms, the UI shows "Telemetry stale" and keeps animating.
- **You can watch it happen.** "Stall 3 s" in the Pipeline card blocks the producer thread. The
  car freezes, the banner appears, and the fps counter and the pulsing live dot keep going.
  `tst_ui_smoke::stalledProducerDoesNotStallRendering` asserts that frames keep coming.
- **Shutdown can't hang either.** The worker's sleep is interruptible
  (`condition_variable_any::wait_until` with a `stop_token`), so `stop()` returns in
  milliseconds even mid-stall.

The general Qt patterns are a worker `QObject` moved to a `QThread` with queued signals, or
`QtConcurrent` / `QFuture` with a `QFutureWatcher` for one-shot work. The rule is the same in
every case: results come back to the GUI thread asynchronously, and nothing on the GUI thread
calls `wait()`.

---

### 3. How would you handle vehicle telemetry arriving faster than the display refresh rate?

**Coalesce per frame and decouple the rates.**

- Every frame, `TelemetryConsumer::drain` takes **all** queued samples in one batch (one acquire
  load and one release store). The frame renders the **newest** state.
- Intermediate samples aren't wasted. They feed the decimated trail (0.5 m spacing) and the
  speed history (20 Hz). Each is a fixed-size ring, so memory is bounded.
- QML sees **one** change notification per frame for all telemetry properties, so bindings
  re-evaluate once per frame however many samples arrived.
- **Backpressure** comes from the bounded queue. If the consumer falls behind, the producer drops
  the newest sample and counts an overflow. Sequence numbers make every loss visible.
- **Evidence:** at 4 kHz telemetry on a 120 Hz display, about 33 samples coalesce per frame and
  the frame rate equals the 240 Hz case.

**Alternative:** a triple-buffered "latest value" slot. It's simpler and O(1) in memory, but it
throws away intermediate samples. Pick it when you only need the newest state.

---

### 4. Explain `unique_ptr` versus `shared_ptr`.

- **`std::unique_ptr<T>`** means sole ownership. It is move-only, the same size as a raw
  pointer, and has no runtime overhead beyond the deleter. It is the default choice.
  - Here: `TelemetryWorker` owns its `std::unique_ptr<TelemetrySource>`, which makes the
    hand-off of the source to the worker thread explicit in the type. `AppContext` holds its
    subsystems in `unique_ptr`s so it controls destruction order and joins the worker first.
- **`std::shared_ptr<T>`** means shared ownership through an atomically reference-counted
  control block. It costs two words per pointer, an extra allocation unless you use
  `make_shared`, an atomic increment on every copy, and lifetimes that are harder to reason
  about. Use it only when ownership is genuinely shared and has no clear last owner.
  - Watch for cycles (fix with `weak_ptr`) and for passing `shared_ptr` by value through hot
    paths.
- **Non-owning access** uses references or raw pointers, or in Qt, `QPointer` for `QObject`s that
  might be destroyed elsewhere. `VehicleSceneItem` holds `QPointer<VehicleViewModel>`.
- This codebase has **zero `shared_ptr`s**. Every object has a single, clear owner, and data
  crosses threads by value.

---

### 5. How do you use AI without compromising software quality?

**AI writes drafts. Tests, tools and measurement decide what ships.**

- **Specify before generating.** Interfaces, invariants and ownership come first. Generated code
  has to fit a design rather than define it.
- **Verify mechanically:**
  - `-Wall -Wextra -Wconversion -Wshadow -Wold-style-cast -Werror` on Clang and GCC
  - ASan, UBSan and TSan in CI
  - Unit tests that pin behaviour: a 2M-item cross-thread queue test, and lane keeping
    held to ≤0.5 m for two minutes
  - A headless UI test that fails on any QML warning
  - Pinned formatting
- **Measure claims.** "Fast" means a benchmark or frame statistics, not an assertion.
- **Read every line that ships, and be able to change it.** For example, the tests caught a stale
  Autopilot-availability state after a gear change. The fix was in the simulator, not in the
  test. I also caught a 60 Hz jank threshold that misjudged a 120 Hz display. AI made the first
  draft quicker to write; finding and fixing these issues took judgment.

---

### 6. How would you diagnose intermittent UI stuttering?

1. **Quantify first.** Use p95, p99 and max frame time plus a jank count against the real
   refresh interval, not average fps. The Performance card and `--benchmark-seconds` provide
   these.
2. **Localize by phase.** Run `QSG_RENDER_TIMING=1` to split sync, render and swap. Our monitor
   separately reports GUI-blocked sync time. If sync is small, look at the GUI thread (QML
   bindings, JS, layout, GC) or the GPU and compositor.
3. **Correlate.** Do hitches line up with periodic work? Here that could be the 150 m road
   rebuild, the 4 Hz stats aggregation, or the 10 Hz chart update. Toggle features to bisect,
   for example by hiding the side panel or disabling the trail.
4. **Profile.** Use Instruments *Animation Hitches* and *Time Profiler* on macOS, or
   `perf` with Hotspot on Linux. Use the QML Profiler for binding and JS cost. Use
   `QSG_RENDERER_DEBUG=render` to check batching.
5. **Check the usual suspects.** These are allocation in the frame path, GC in JS-heavy QML,
   texture or glyph uploads, priority inversion on a lock shared with a background thread (there
   are no such locks here), and thermal or power throttling.
6. **Fix, then prove it** with the same measurement, and add a regression guard: a benchmark
   threshold or a test.

This project's own open item is a worked example. About 5% of frames miss the 120 Hz deadline
while sync costs 0.2 ms, so the investigation moves to QML-side and compositor costs. See
[PERFORMANCE.md](PERFORMANCE.md#reading-these-numbers).

---

## Live demo script (5 minutes)

1. **Launch.** The car is on Autopilot with traffic. Point out fps, p99, sync cost and RSS.
2. **Lane change.** Press `Q`. The signal blinks, the planned path shifts, the car merges, and
   the signal cancels itself.
3. **Driver override.** Press `S`. Autopilot disengages to Standby. Drive manually with `WASD`.
4. **Stress the pipeline.** Select 4 kHz. Samples per frame jump and fps doesn't move.
5. **Break the backend.** Press *Stall 3 s*. The car freezes, the stale banner appears, and
   the UI stays at full frame rate.
6. **Lose data.** Press *Drop 30%*. The gap counter climbs and the picture stays smooth.
7. **Show the code** behind each step: `spsc_ring_buffer.hpp`,
   `TelemetryWorker::run`, and `VehicleSceneItem::updatePaintNode`.
8. **Change something live**, such as lane width, the trail colour or the IDM time headway.
   Rebuild, and run the tests.
