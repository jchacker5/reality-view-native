# Architecture

This document covers what the system is, why it is shaped this way, and what breaks it.

## Goals and constraints

1. **The UI never waits on data.** A slow, stalled or bursting producer must not cost a frame.
2. **Constant per-frame work.** Frame cost must not scale with the telemetry rate.
3. **No locks on the hot path.** Per-sample operations are wait-free; locks exist only for cold
   paths, such as the condition variable used to sleep.
4. **Measurable.** Every performance claim comes from a counter in the app or a benchmark.
5. **Testable without a GPU.** Everything except QML lives in a Qt-free core library.

## Threads

| Thread | Owns | Talks to others through |
|---|---|---|
| Telemetry (`std::thread`) | `TelemetrySource` (simulator or replay) | Sample SPSC (out), command SPSC (in), atomics |
| GUI (main) | `VehicleViewModel`, `TelemetryConsumer`, QML, `FrameMonitor` | Drains the sample SPSC; posts to the command SPSC |
| Render (Qt's threaded loop) | Scene-graph nodes | `updatePaintNode` during sync (GUI blocked); frame timings via SPSC |

Each piece of mutable state has exactly one owning thread. Data crosses threads by value
through single-producer/single-consumer queues, so there is no shared mutable object to protect.

### The producer loop (`TelemetryWorker::run`)

```
loop until stop requested:
    apply every pending command          // pipeline commands here, vehicle commands to the source
    source.step(1 / rate)                // fixed step: deterministic regardless of jitter
    sample = source.sample(); stamp seq and steady-clock time
    queue.tryPush(sample) or count an overflow
    next += period; resync if > 100 ms behind; sleep until next (interruptible)
```

- **Absolute deadlines** (`sleep_until`) keep per-iteration work from accumulating as drift.
- **Resync instead of catch-up.** After a stall the loop does not replay a burst of stale steps.
  For a display, a gap is better than a fast-forward.
- **Interruptible sleep.** `condition_variable::wait_until(lock, deadline, stopRequested)`.
  `stop()` sets the flag *under the mutex* and then notifies, so a stop can't slip between
  the predicate check and the wait (the classic lost wake-up). Shutdown is prompt even mid-stall. A test enforces it: under 500 ms during
  a 10 s stall.
- **Overflow policy: drop newest, count it.** When the consumer falls behind, the producer
  refuses the sample and increments `overflowCount`. The consumer sees it as a sequence gap. The
  alternative, overwriting the oldest, is unsafe in an SPSC design because the producer would
  write the slot the consumer is reading.

### The consumer (`VehicleViewModel::tick` → `TelemetryConsumer::drain`)

QML's `FrameAnimation` calls `tick()` once per vsync on the GUI thread. `tick()`:

1. Drains everything queued, with one acquire load and one release store for the batch.
2. Keeps only what a frame needs: the newest sample, a trail decimated to 0.5 m, and a speed
   history decimated to 20 Hz, all in fixed-size rings.
3. Detects gaps from sequence numbers.
4. Emits **one** `telemetryChanged` signal. All 18 telemetry properties share that NOTIFY, so
   each binding re-evaluates at most once per frame, whether 2 or 70 samples arrived.

Pipeline health (rate, gaps, age, drain cost) is published at 4 Hz and chart series at 10 Hz,
on separate signals, so slow-moving data doesn't churn bindings every frame.

### The SPSC ring buffer

`src/core/spsc_ring_buffer.hpp`. A bounded, wait-free ring buffer with a power-of-two
capacity.

- **Free-running `size_t` counters**, masked to index. Unsigned wrap-around keeps `head - tail`
  correct forever, and full and empty are distinguishable without a wasted slot.
- **Memory ordering:** the producer writes the slot, then `head.store(release)`. The consumer
  does `head.load(acquire)`, then reads the slot. The symmetric pair on `tail` returns slots to
  the producer. Nothing stronger is needed with one producer and one consumer.
- **Cached opposite index.** Each side keeps a private copy of the other side's counter and
  re-reads the shared atomic only when the queue looks full (producer) or empty (consumer). The
  common case touches only the thread's own cache line.
- **128-byte alignment** of `head`, `tail` and storage. Apple silicon and some x86 parts
  prefetch adjacent line pairs, so 64-byte padding can still false-share.
- **Batch drain** amortizes the release store over every item consumed in a frame.

Measured at 159 M items/s across two threads, against 6.7 M for `std::mutex` plus
`std::deque`. A test streams 2 million items between two threads and checks ordering; CI runs
it under ThreadSanitizer.

## Rendering

`VehicleSceneItem` is a `QQuickItem` with `ItemHasContents`. It builds its own scene-graph
subtree: one `QSGTransformNode` (the camera) over five `QSGGeometryNode`s for road, planned
path, trail, traffic and ego car.

**Sync contract.** `updatePaintNode` runs on the render thread while the GUI thread is blocked.
It is the only moment both threads touch the item, so it may read GUI-owned members without a
lock. It must not read the view model directly, and it doesn't. On `telemetryChanged` the item
copies what it needs into a snapshot (`snapshot()`, GUI thread). Allocation-heavy work, such as
extending the road, also happens there.

**Per-frame cost control:**

| Mesh | Size | Rebuilt |
|---|---|---|
| Road (surface, shoulders, edges, dashed lanes) | ~26k vertices | Every **150 m**, not every frame |
| Camera | 1 matrix | Every frame |
| Ego car, traffic, planned path | ~100–1000 vertices | Every frame |
| Trail | ≤ 2048 points | Every frame |

Between road rebuilds, motion is a single matrix change on the transform node. Dynamic meshes
are built into a reused `std::vector` and uploaded with one `memcpy`.
`ColoredVertex` is static-asserted to be byte-compatible with `QSGGeometry::ColoredPoint2D`.
Dynamic geometry is tagged `StreamPattern` and the road `StaticPattern`. Every mesh uses the
same vertex format and material type, so Qt's batch renderer can merge them.

**Camera transform** (heading-up chase view):

```
screen = T(anchor) · S(ppm, −ppm) · R(90° − heading) · T(−(ego − origin))
```

The y-flip maps world north-up to screen y-down. The rotation puts the car's heading at
screen-up. The 3D look is a Qt Quick `Rotation` about the x axis, which applies a projective
transform at no extra cost.

**Floating origin.** World positions are `double`. Vertices are `float` offsets from an origin
re-based on every road rebuild. A float has 24 mantissa bits, so at 100 km from the origin it
resolves about 8 mm, and the error compounds through transforms. A test builds a mesh 55 km
into a drive and checks that every vertex stays within ±300 m.

**Premultiplied alpha.** `QSGVertexColorMaterial` expects premultiplied colours, so the mesh
builders emit them (`Rgba::premultiplied`, unit-tested).

## Ownership and lifetimes

- `AppContext` is the composition root. It builds the source, then the worker, then the view
  model and monitor. It destroys them in reverse and **explicitly stops the worker first**, so
  the thread is joined before anything that references its queues is destroyed.
- `TelemetryWorker` owns its source through `std::unique_ptr<TelemetrySource>`. The source is
  touched only by the worker thread after `start()`. The roughly 1.7 MB sample queue is heap-
  allocated so it never lands on a caller's stack.
- The worker's destructor stops and joins (RAII), so a worker thread can never outlive its
  owner. This is the `std::jthread` + `stop_token` contract, written out by hand because
  Apple's libc++ before LLVM 20 ships those only as experimental. CI caught this on Xcode 16.4.
- Scene-graph nodes are owned by Qt (`OwnedByParent`, `OwnsGeometry`, `OwnsMaterial`). If the
  graph is invalidated, the item receives `nullptr` as `oldNode` and rebuilds everything.
- QML objects that C++ creates are injected as **required properties**
  (`setInitialProperties`), not context properties. The dependency is explicit and visible to
  tooling.

## Determinism

The road and traffic come from a seed via SplitMix64, not
`std::uniform_real_distribution`, whose output is implementation-defined and differs between
libc++ and libstdc++. The simulator uses a fixed time step. As a result:

- Tests can assert exact positions after 30 simulated seconds.
- **The renderer rebuilds the same road from the seed**, so the road is never shared between
  threads, and never sent over the queue.

## Failure modes

| Failure | Behaviour | Where it's tested |
|---|---|---|
| Producer stalls (slow backend, GC pause) | UI keeps rendering at full rate; "Telemetry stale" banner after 250 ms; car holds last pose | `tst_telemetry_pipeline`, `tst_ui_smoke` |
| Samples lost in transit | Detected from `seq` gaps, counted, logged at debug level | `dropoutIsDetectedAsSequenceGaps` |
| Consumer falls behind | Producer drops newest and counts overflow; no corruption | `slowConsumerOverflowsWithoutCorruption` |
| Telemetry far faster than vsync | Coalesced per frame; constant UI cost | `deliversSamplesInOrderWithoutGaps`, benchmarks |
| Shutdown during a stall | Interruptible sleep; join under 500 ms | `stopIsPromptEvenDuringStall` |
| Replay file missing or malformed | Exit code 2 with a message naming the line | `tst_telemetry_csv` |
| Reset or replay loop (position jumps) | Trail and odometer ignore the teleport | `TelemetryConsumer::ingest` |
| Command queue full | Command dropped with a warning; UI unaffected | `VehicleViewModel::post` |

## What I would do next

- **Use a triple-buffered "latest value" slot alongside the queue** for consumers that only want
  the newest state. It costs O(1) memory instead of a 4096-slot ring, at the price of losing the
  intermediate samples the trail uses.
- **Move the trail to an incrementally appended ring buffer** in GPU memory. It is the largest
  per-frame mesh at 36 µs for 2048 points.
- **Add a Quick3D (or custom RHI) renderer** behind the same snapshot interface, reusing the
  splat assets from Open Tesla Reality View.
- **Decode real telemetry**: the original project's SEI decoder could feed a `TelemetrySource`
  directly.
