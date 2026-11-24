// Boots the real QML UI against the real pipeline, headless, and checks the integration
// points unit tests cannot: QML loads without warnings, frames are produced, the scene item's
// updatePaintNode runs, commands flow UI -> worker, and a stalled producer does not stall
// rendering.
#include "app_context.hpp"
#include "vehicle_scene_item.hpp"

#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QtQml/QQmlExtensionPlugin>
#include <QtTest>

Q_IMPORT_QML_PLUGIN(RealityViewPlugin)

using namespace rvn;

class TstUiSmoke : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<AppContext> context_;
    std::unique_ptr<QQmlApplicationEngine> engine_;
    QQuickWindow* window_{nullptr};

private slots:
    void initTestCase() {
        // Any QML warning (binding error, missing property, type error) fails the test.
        QTest::failOnWarning(QRegularExpression(QStringLiteral("qrc:|\\.qml|QML|ReferenceError|TypeError")));

        AppOptions options;
        options.publishRateHz = 500;
        context_ = std::make_unique<AppContext>(options);
        engine_ = std::make_unique<QQmlApplicationEngine>();
        engine_->setInitialProperties({
            {QStringLiteral("vehicle"), QVariant::fromValue(context_->vehicle())},
            {QStringLiteral("monitor"), QVariant::fromValue(context_->monitor())},
        });
        engine_->loadFromModule("RealityView", "Main");
        QCOMPARE(engine_->rootObjects().size(), 1);
        window_ = qobject_cast<QQuickWindow*>(engine_->rootObjects().constFirst());
        QVERIFY(window_ != nullptr);
        context_->monitor()->attach(window_);
        QVERIFY(QTest::qWaitForWindowExposed(window_));
    }

    void telemetryReachesTheUi() {
        QTRY_VERIFY_WITH_TIMEOUT(context_->vehicle()->hasData(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(context_->vehicle()->speedMph() > 10.0, 5000);
        QCOMPARE(context_->vehicle()->autopilotText(), QStringLiteral("Active"));
    }

    void sceneGraphIsBuilt() {
        auto* scene = window_->findChild<VehicleSceneItem*>();
        QVERIFY(scene != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(scene->roadRebuilds() >= 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(context_->monitor()->totalFrames() > 20, 5000);
    }

    void commandsRoundTrip() {
        context_->vehicle()->toggleAutopilot(); // disengage
        QTRY_COMPARE_WITH_TIMEOUT(context_->vehicle()->autopilotText(), QStringLiteral("Standby"), 3000);
        context_->vehicle()->toggleAutopilot(); // re-engage
        QTRY_COMPARE_WITH_TIMEOUT(context_->vehicle()->autopilotText(), QStringLiteral("Active"), 3000);
        context_->vehicle()->setPublishRate(1000);
        QTRY_COMPARE_WITH_TIMEOUT(context_->vehicle()->publishRateHz(), 1000, 3000);
    }

    void stalledProducerDoesNotStallRendering() {
        QTRY_VERIFY_WITH_TIMEOUT(!context_->vehicle()->stale(), 3000);
        context_->vehicle()->injectStall(1500);
        QTRY_VERIFY_WITH_TIMEOUT(context_->vehicle()->stale(), 3000);
        const double framesAtStall = context_->monitor()->totalFrames();
        QTest::qWait(600);
        QVERIFY2(context_->monitor()->totalFrames() > framesAtStall + 10,
                 "the render loop must keep producing frames while telemetry is stalled");
        QTRY_VERIFY_WITH_TIMEOUT(!context_->vehicle()->stale(), 5000);
    }

    void cleanupTestCase() {
        engine_.reset();
        context_.reset(); // joins the worker thread
    }
};

QTEST_MAIN(TstUiSmoke)
#include "tst_ui_smoke.moc"
