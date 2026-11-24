#pragma once

#include "road.hpp"
#include "scene_geometry.hpp"
#include "telemetry.hpp"

#include <QPointer>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <vector>

namespace rvn {

class VehicleViewModel;

/// The vehicle visualization: road, lanes, traffic, planned path, trail and the ego car,
/// drawn straight into the Qt Quick scene graph (no QPainter, no Canvas, no per-object
/// QML items).
///
/// Threading contract (the part interviewers poke at):
///  * GUI thread: snapshot() copies what the next frame needs out of the view model.
///  * updatePaintNode() runs on the render thread *while the GUI thread is blocked* in the
///    sync phase, so it may read the snapshot without locks. It never touches the view model.
///
/// Performance:
///  * The road mesh (~25k vertices) is rebuilt only when the car drives >150 m from where it
///    was last built. Every frame only the camera matrix on a QSGTransformNode changes.
///  * Small dynamic meshes (car, traffic, trail) are rebuilt every frame into reused vectors
///    and uploaded with one memcpy; their geometry is flagged StreamPattern.
///  * All meshes share one vertex format and material type, so the batch renderer can merge them.
///  * Floating origin: world coordinates are doubles; vertices are float offsets from a nearby
///    origin, so precision does not degrade 100 km into a drive.
class VehicleSceneItem : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(rvn::VehicleViewModel* vehicle READ vehicle WRITE setVehicle NOTIFY vehicleChanged)
    Q_PROPERTY(double pixelsPerMeter READ pixelsPerMeter WRITE setPixelsPerMeter NOTIFY pixelsPerMeterChanged)
    Q_PROPERTY(double egoAnchorY READ egoAnchorY WRITE setEgoAnchorY NOTIFY egoAnchorYChanged)
    Q_PROPERTY(bool showTrail READ showTrail WRITE setShowTrail NOTIFY showTrailChanged)
    Q_PROPERTY(int roadRebuilds READ roadRebuilds NOTIFY roadRebuildsChanged)

public:
    explicit VehicleSceneItem(QQuickItem* parent = nullptr);
    ~VehicleSceneItem() override;

    [[nodiscard]] VehicleViewModel* vehicle() const { return vehicle_; }
    void setVehicle(VehicleViewModel* vehicle);
    [[nodiscard]] double pixelsPerMeter() const { return pixelsPerMeter_; }
    void setPixelsPerMeter(double ppm);
    [[nodiscard]] double egoAnchorY() const { return egoAnchorY_; }
    void setEgoAnchorY(double y);
    [[nodiscard]] bool showTrail() const { return showTrail_; }
    void setShowTrail(bool show);
    [[nodiscard]] int roadRebuilds() const { return roadRebuilds_; }

signals:
    void vehicleChanged();
    void pixelsPerMeterChanged();
    void egoAnchorYChanged();
    void showTrailChanged();
    void roadRebuildsChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;

private:
    void snapshot();

    QPointer<VehicleViewModel> vehicle_;
    double pixelsPerMeter_{9.0};
    double egoAnchorY_{0.72};
    bool showTrail_{true};

    // ---- Snapshot (written on GUI thread, read during sync) ------------------------------
    bool hasSample_{false};
    TelemetrySample sample_{};
    std::vector<Vec2> trail_;
    std::unique_ptr<Road> road_;

    // ---- Render-side caches (only touched in updatePaintNode) ----------------------------
    Vec2 origin_{};
    double roadBuiltAtS_{-1e9};
    bool roadDirty_{true};
    int roadRebuilds_{0};
    std::vector<ColoredVertex> scratch_;
};

} // namespace rvn
