#include "app_context.hpp"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QTimer>
#include <QtQml/QQmlExtensionPlugin>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

Q_LOGGING_CATEGORY(lcApp, "rvn.app")

// The QML module is a static library; make its types and files visible to the engine.
Q_IMPORT_QML_PLUGIN(RealityViewPlugin)

namespace {

/// The surface format must be set before QGuiApplication exists, so --msaa is read from argv
/// directly (QCommandLineParser below still documents and validates it).
int msaaSamplesFromArgs(int argc, char* argv[]) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--msaa") == 0) {
            return std::clamp(std::atoi(argv[i + 1]), 0, 8);
        }
    }
    return 4;
}

} // namespace

int main(int argc, char* argv[]) {
    // MSAA smooths road edges and car outlines, at a fill-rate cost (see docs/PERFORMANCE.md).
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setSamples(msaaSamplesFromArgs(argc, argv));
    QSurfaceFormat::setDefaultFormat(format);

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("Reality View Native"));
    QGuiApplication::setApplicationVersion(QStringLiteral(RVN_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Real-time vehicle visualization with simulated telemetry"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption seedOpt(QStringLiteral("seed"), QStringLiteral("Road and traffic seed."),
                                     QStringLiteral("n"), QStringLiteral("7"));
    const QCommandLineOption rateOpt(QStringLiteral("rate"), QStringLiteral("Telemetry publish rate in Hz (10-4000)."),
                                     QStringLiteral("hz"), QStringLiteral("240"));
    const QCommandLineOption replayOpt(QStringLiteral("replay"),
                                       QStringLiteral("Replay a recorded telemetry CSV instead of simulating."),
                                       QStringLiteral("file"));
    const QCommandLineOption benchOpt(QStringLiteral("benchmark-seconds"),
                                      QStringLiteral("Run for N seconds, print frame statistics as JSON, and exit."),
                                      QStringLiteral("n"));
    const QCommandLineOption shotOpt(QStringLiteral("screenshot"),
                                     QStringLiteral("Save a PNG of the window after 3 s, then exit."),
                                     QStringLiteral("file"));
    const QCommandLineOption msaaOpt(QStringLiteral("msaa"),
                                     QStringLiteral("Multisample anti-aliasing samples: 0, 2, 4 or 8."),
                                     QStringLiteral("n"), QStringLiteral("4"));
    const QCommandLineOption viewOpt(QStringLiteral("view"),
                                     QStringLiteral("Initial camera: 3d (tilted) or 2d (top-down)."),
                                     QStringLiteral("mode"), QStringLiteral("3d"));
    const QCommandLineOption panelOpt(QStringLiteral("panel"),
                                      QStringLiteral("Show the side panel (on) or the visualization only (off)."),
                                      QStringLiteral("on|off"), QStringLiteral("on"));
    parser.addOptions({seedOpt, rateOpt, replayOpt, benchOpt, shotOpt, msaaOpt, viewOpt, panelOpt});
    parser.process(app);

    rvn::AppOptions options;
    options.seed = parser.value(seedOpt).toULongLong();
    options.publishRateHz = parser.value(rateOpt).toInt();
    options.replayPath = parser.value(replayOpt);

    std::unique_ptr<rvn::AppContext> context;
    try {
        context = std::make_unique<rvn::AppContext>(options);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 2;
    }

    QQmlApplicationEngine engine;
    engine.setInitialProperties({
        {QStringLiteral("vehicle"), QVariant::fromValue(context->vehicle())},
        {QStringLiteral("monitor"), QVariant::fromValue(context->monitor())},
        {QStringLiteral("startTilted"), parser.value(viewOpt) != QStringLiteral("2d")},
        {QStringLiteral("showPanel"), parser.value(panelOpt) != QStringLiteral("off")},
    });
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    engine.loadFromModule("RealityView", "Main");
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    if (window == nullptr) {
        qCCritical(lcApp) << "root object is not a window";
        return 1;
    }
    context->monitor()->attach(window);
    context->start();
    // Bring the window forward: macOS throttles occluded windows, which would skew measurements.
    window->raise();
    window->requestActivate();

    if (parser.isSet(benchOpt)) {
        const int seconds = std::max(1, parser.value(benchOpt).toInt());
        // Discard the first second (shader compilation, first uploads) from the statistics.
        QTimer::singleShot(1000, context->monitor(), &rvn::FrameMonitor::resetStats);
        QTimer::singleShot((seconds + 1) * 1000, &app, [&context] {
            std::printf("%s\n", qPrintable(context->monitor()->summaryJson()));
            std::fflush(stdout);
            QCoreApplication::quit();
        });
    }

    if (parser.isSet(shotOpt)) {
        const QString path = parser.value(shotOpt);
        QTimer::singleShot(3000, window, [window, path] {
            const bool ok = window->grabWindow().save(path);
            std::fprintf(ok ? stdout : stderr, "%s %s\n", ok ? "saved" : "failed to save", qPrintable(path));
            QCoreApplication::exit(ok ? 0 : 1);
        });
    }

    const int rc = QGuiApplication::exec();
    engine.clearComponentCache();
    return rc;
}
