// Control experiment for frame pacing: a near-empty Qt Quick window (one animated rectangle)
// measured with the same FrameStats code as the app. If this misses frames at the same rate as
// reality-view-native, the misses come from the platform (compositor, GPU scheduling, other
// load), not from the app's content.
//
//   pacing_probe [seconds]   -> prints the same JSON fields as --benchmark-seconds
#include "frame_stats.hpp"

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    const int seconds = argc > 1 ? std::max(1, std::atoi(argv[1])) : 8;

    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        Window {
            width: 1480; height: 900; visible: true; color: "#0b0d10"
            Rectangle {
                width: 120; height: 120; radius: 16; color: "#3d7bff"
                anchors.centerIn: parent
                RotationAnimation on rotation { from: 0; to: 360; duration: 2000; loops: Animation.Infinite }
            }
        })",
                      QUrl());
    std::unique_ptr<QObject> root(component.create());
    auto* window = qobject_cast<QQuickWindow*>(root.get());
    if (window == nullptr) {
        std::fprintf(stderr, "failed to create probe window: %s\n", qPrintable(component.errorString()));
        return 1;
    }
    window->raise();
    window->requestActivate();

    const double refresh = window->screen() != nullptr ? window->screen()->refreshRate() : 60.0;
    rvn::FrameStats stats(100000, 1000.0 / refresh);
    std::mutex m; // probe only: simplicity over the app's lock-free hand-off
    std::chrono::steady_clock::time_point last{};
    bool measuring = false;
    QObject::connect(
        window, &QQuickWindow::frameSwapped, window,
        [&] {
            const auto now = std::chrono::steady_clock::now();
            const std::lock_guard lock(m);
            if (measuring && last != std::chrono::steady_clock::time_point{}) {
                stats.addFrame(std::chrono::duration<double, std::milli>(now - last).count());
            }
            last = now;
        },
        Qt::DirectConnection);

    QTimer::singleShot(1000, window, [&] {
        const std::lock_guard lock(m);
        measuring = true; // skip startup, like the app's benchmark mode
    });
    QTimer::singleShot((seconds + 1) * 1000, &app, [&] {
        rvn::FrameStats::Summary s;
        {
            // Copy under the lock, then release it BEFORE calling quit(): on macOS quit()
            // synchronously closes the window and waits for the render thread, which may be
            // blocked on this very mutex in frameSwapped. Holding it here deadlocks.
            const std::lock_guard lock(m);
            measuring = false;
            s = stats.summary();
        }
        std::printf("{\"fps\": %.3f, \"frame_ms_p50\": %.3f, \"frame_ms_p95\": %.3f, \"frame_ms_p99\": %.3f, "
                    "\"frames\": %llu, \"jank_frames_total\": %llu, \"target_frame_ms\": %.3f}\n",
                    s.fps, s.p50Ms, s.p95Ms, s.p99Ms, static_cast<unsigned long long>(s.totalFrames),
                    static_cast<unsigned long long>(s.totalJank), stats.targetFrameMs());
        std::fflush(stdout);
        QCoreApplication::quit();
    });
    const int rc = QGuiApplication::exec();
    // Tear the window (and its render thread) down while the state its frameSwapped handler
    // captures by reference is still alive: those locals are declared after `root`, so they
    // would otherwise be destroyed first.
    QObject::disconnect(window, &QQuickWindow::frameSwapped, nullptr, nullptr);
    root.reset();
    return rc;
}
