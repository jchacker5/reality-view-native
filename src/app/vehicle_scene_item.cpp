#include "vehicle_scene_item.hpp"

#include "vehicle_view_model.hpp"

#include <QMatrix4x4>
#include <QSGGeometryNode>
#include <QSGTransformNode>
#include <QSGVertexColorMaterial>

#include <cmath>
#include <cstring>
#include <numbers>

namespace rvn {
namespace {

constexpr double kRoadBehindM = 250.0;
constexpr double kRoadAheadM = 550.0;
constexpr double kRebuildDistanceM = 150.0;

/// One vertex-coloured triangle mesh in the scene graph.
QSGGeometryNode* makeMeshNode(QSGGeometry::DataPattern pattern) {
    auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    geometry->setVertexDataPattern(pattern);
    auto* node = new QSGGeometryNode;
    node->setGeometry(geometry);
    node->setMaterial(new QSGVertexColorMaterial);
    node->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial);
    return node;
}

void upload(QSGGeometryNode* node, const std::vector<ColoredVertex>& vertices) {
    QSGGeometry* g = node->geometry();
    const int count = static_cast<int>(vertices.size());
    if (g->vertexCount() != count) {
        g->allocate(count); // reallocates only when the size changes
    }
    if (count > 0) {
        static_assert(sizeof(ColoredVertex) == sizeof(QSGGeometry::ColoredPoint2D));
        std::memcpy(g->vertexData(), vertices.data(), vertices.size() * sizeof(ColoredVertex));
    }
    node->markDirty(QSGNode::DirtyGeometry);
}

/// Root of our subtree. Holding the child pointers avoids walking the node tree each frame.
struct SceneRoot : QSGTransformNode {
    QSGGeometryNode* road = makeMeshNode(QSGGeometry::StaticPattern);
    QSGGeometryNode* plannedPath = makeMeshNode(QSGGeometry::StreamPattern);
    QSGGeometryNode* trail = makeMeshNode(QSGGeometry::StreamPattern);
    QSGGeometryNode* traffic = makeMeshNode(QSGGeometry::StreamPattern);
    QSGGeometryNode* ego = makeMeshNode(QSGGeometry::StreamPattern);

    SceneRoot() {
        // Draw order = child order (back to front).
        for (QSGNode* n : {static_cast<QSGNode*>(road), static_cast<QSGNode*>(plannedPath),
                           static_cast<QSGNode*>(trail), static_cast<QSGNode*>(traffic), static_cast<QSGNode*>(ego)}) {
            appendChildNode(n);
            n->setFlag(QSGNode::OwnedByParent);
        }
    }
};

} // namespace

VehicleSceneItem::VehicleSceneItem(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    trail_.reserve(TelemetryConsumer::kTrailCapacity);
    scratch_.reserve(32 * 1024);
}

VehicleSceneItem::~VehicleSceneItem() = default;

void VehicleSceneItem::setVehicle(VehicleViewModel* vehicle) {
    if (vehicle_ == vehicle) {
        return;
    }
    if (vehicle_) {
        disconnect(vehicle_, nullptr, this, nullptr);
    }
    vehicle_ = vehicle;
    if (vehicle_) {
        road_ = std::make_unique<Road>(static_cast<std::uint64_t>(vehicle_->roadSeed()));
        roadDirty_ = true;
        connect(vehicle_, &VehicleViewModel::telemetryChanged, this, &VehicleSceneItem::snapshot);
    }
    emit vehicleChanged();
}

void VehicleSceneItem::setPixelsPerMeter(double ppm) {
    ppm = std::clamp(ppm, 1.0, 60.0);
    if (qFuzzyCompare(ppm, pixelsPerMeter_)) {
        return;
    }
    pixelsPerMeter_ = ppm;
    emit pixelsPerMeterChanged();
    update();
}

void VehicleSceneItem::setEgoAnchorY(double y) {
    y = std::clamp(y, 0.0, 1.0);
    if (qFuzzyCompare(y, egoAnchorY_)) {
        return;
    }
    egoAnchorY_ = y;
    emit egoAnchorYChanged();
    update();
}

void VehicleSceneItem::setShowTrail(bool show) {
    if (show == showTrail_) {
        return;
    }
    showTrail_ = show;
    emit showTrailChanged();
    update();
}

void VehicleSceneItem::snapshot() {
    // GUI thread. Copy just what the next frame needs; the render thread never sees the model.
    const auto& latest = vehicle_->consumer().latest();
    if (!latest || !road_) {
        return;
    }
    sample_ = *latest;
    hasSample_ = true;
    trail_.clear();
    vehicle_->consumer().trail().copyTo(std::back_inserter(trail_));
    // Extending the road allocates, so it happens here rather than on the render thread.
    road_->ensureLength(static_cast<double>(sample_.roadS) + kRoadAheadM + 50.0);
    update();
}

QSGNode* VehicleSceneItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* /*data*/) {
    auto* root = static_cast<SceneRoot*>(oldNode);
    if (root == nullptr) {
        root = new SceneRoot;
        roadDirty_ = true;
    }
    if (!hasSample_ || !road_) {
        return root;
    }

    const Vec2 ego{sample_.x, sample_.y};
    const double egoS = static_cast<double>(sample_.roadS);

    // 1. Road: rebuild rarely, and re-base the floating origin when we do.
    if (roadDirty_ || std::abs(egoS - roadBuiltAtS_) > kRebuildDistanceM) {
        origin_ = ego;
        scratch_.clear();
        buildRoadMesh(*road_, egoS - kRoadBehindM, egoS + kRoadAheadM, origin_, scratch_);
        upload(root->road, scratch_);
        roadBuiltAtS_ = egoS;
        roadDirty_ = false;
        ++roadRebuilds_;
        QMetaObject::invokeMethod(this, &VehicleSceneItem::roadRebuildsChanged, Qt::QueuedConnection);
    }

    // 2. Planned path, only while the driver-assist stack is steering.
    scratch_.clear();
    if (sample_.autopilot == AutopilotState::Active) {
        const double laneCenter = road_->laneCenterOffset(sample_.lane);
        buildPlannedPathMesh(*road_, egoS + 2.0, 70.0, laneCenter, origin_, Rgba{61, 123, 255, 150}, scratch_);
    }
    upload(root->plannedPath, scratch_);

    // 3. Trail.
    scratch_.clear();
    if (showTrail_) {
        buildTrailMesh(trail_, 0.5, origin_, scratch_);
    }
    upload(root->trail, scratch_);

    // 4. Traffic.
    scratch_.clear();
    buildTrackedObjectsMesh(std::span<const TrackedObject>(sample_.objects.data(), sample_.objectCount), origin_,
                            scratch_);
    upload(root->traffic, scratch_);

    // 5. Ego car. Turn signals blink at 1.4 Hz on simulation time, so a paused sim stops blinking.
    scratch_.clear();
    {
        MeshBuilder mb(scratch_, origin_);
        VehicleAppearance look;
        look.roadWheelAngle = static_cast<double>(sample_.steeringWheelDeg) / 15.0 * std::numbers::pi / 180.0;
        const bool blinkOn = std::fmod(sample_.simTimeS, 0.7) < 0.35;
        look.lights.braking = sample_.brake > 0.05F || (sample_.speedMps < 0.1F && sample_.gear == Gear::Drive);
        look.lights.leftSignalOn = blinkOn && sample_.turnSignal == TurnSignal::Left;
        look.lights.rightSignalOn = blinkOn && sample_.turnSignal == TurnSignal::Right;
        look.lights.reversing = sample_.gear == Gear::Reverse;
        appendVehicleMesh(mb, Pose2{ego, static_cast<double>(sample_.heading)}, look);
    }
    upload(root->ego, scratch_);

    // 6. Camera: heading-up chase view centred on the car.
    //    screen = T(anchor) * S(ppm, -ppm) * R(90deg - heading) * T(-(ego - origin))
    //    The y flip turns world "north-up" into screen "y-down"; the rotation puts the car's
    //    heading at screen-up.
    QMatrix4x4 m;
    m.translate(static_cast<float>(width() * 0.5), static_cast<float>(height() * egoAnchorY_));
    m.scale(static_cast<float>(pixelsPerMeter_), static_cast<float>(-pixelsPerMeter_));
    m.rotate(static_cast<float>(90.0 - static_cast<double>(sample_.heading) * 180.0 / std::numbers::pi), 0.0F, 0.0F,
             1.0F);
    m.translate(static_cast<float>(-(ego.x - origin_.x)), static_cast<float>(-(ego.y - origin_.y)));
    root->setMatrix(m);
    return root;
}

} // namespace rvn
