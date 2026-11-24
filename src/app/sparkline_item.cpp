#include "sparkline_item.hpp"

#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>

#include <algorithm>
#include <cmath>

namespace rvn {
namespace {

QSGGeometryNode* makeNode(QSGGeometry::DrawingMode mode) {
    auto* g = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
    g->setDrawingMode(mode);
    g->setVertexDataPattern(QSGGeometry::StreamPattern);
    auto* n = new QSGGeometryNode;
    n->setGeometry(g);
    n->setMaterial(new QSGFlatColorMaterial);
    n->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial | QSGNode::OwnedByParent);
    return n;
}

void setColor(QSGGeometryNode* n, const QColor& c) {
    auto* m = static_cast<QSGFlatColorMaterial*>(n->material());
    if (m->color() != c) {
        m->setColor(c);
        n->markDirty(QSGNode::DirtyMaterial);
    }
}

} // namespace

SparklineItem::SparklineItem(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    connect(this, &SparklineItem::rangeChanged, this, &QQuickItem::update);
    connect(this, &SparklineItem::colorChanged, this, &QQuickItem::update);
}

void SparklineItem::setValues(const QList<float>& values) {
    values_ = values; // implicitly shared: O(1)
    emit valuesChanged();
    update();
}

QSGNode* SparklineItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* /*data*/) {
    QSGNode* root = oldNode;
    if (root == nullptr) {
        root = new QSGNode;
        root->appendChildNode(makeNode(QSGGeometry::DrawTriangleStrip)); // area
        root->appendChildNode(makeNode(QSGGeometry::DrawTriangleStrip)); // line
        root->appendChildNode(makeNode(QSGGeometry::DrawTriangles));     // threshold
    }
    auto* area = static_cast<QSGGeometryNode*>(root->childAtIndex(0));
    auto* line = static_cast<QSGGeometryNode*>(root->childAtIndex(1));
    auto* guide = static_cast<QSGGeometryNode*>(root->childAtIndex(2));

    const auto w = static_cast<float>(width());
    const auto h = static_cast<float>(height());
    const qsizetype n = values_.size();

    double lo = minValue_;
    double hi = maxValue_;
    if (autoRange_ && n > 0) {
        const auto [mn, mx] = std::minmax_element(values_.cbegin(), values_.cend());
        lo = std::min(lo, static_cast<double>(*mn));
        hi = std::max(hi, static_cast<double>(*mx) * 1.1);
    }
    if (hi - lo < 1e-9) {
        hi = lo + 1.0;
    }
    const auto yOf = [&](double v) { return h - static_cast<float>((v - lo) / (hi - lo)) * h; };

    QSGGeometry* ag = area->geometry();
    QSGGeometry* lg = line->geometry();
    if (n >= 2 && w > 0 && h > 0) {
        ag->allocate(static_cast<int>(n * 2));
        lg->allocate(static_cast<int>(n * 2));
        auto* av = ag->vertexDataAsPoint2D();
        auto* lv = lg->vertexDataAsPoint2D();
        constexpr float kHalfLine = 1.0F;
        for (qsizetype i = 0; i < n; ++i) {
            const float x = w * static_cast<float>(i) / static_cast<float>(n - 1);
            const float y = std::clamp(yOf(static_cast<double>(values_[i])), 0.0F, h);
            av[2 * i].set(x, y);
            av[2 * i + 1].set(x, h);
            lv[2 * i].set(x, y - kHalfLine);
            lv[2 * i + 1].set(x, y + kHalfLine);
        }
    } else {
        ag->allocate(0);
        lg->allocate(0);
    }
    area->markDirty(QSGNode::DirtyGeometry);
    line->markDirty(QSGNode::DirtyGeometry);

    QColor fill = color_;
    fill.setAlphaF(0.18F);
    // Flat colour material expects a straight (non-premultiplied) colour; Qt premultiplies.
    setColor(area, fill);
    setColor(line, color_);
    setColor(guide, thresholdColor_);

    QSGGeometry* gg = guide->geometry();
    if (!std::isnan(threshold_) && threshold_ >= lo && threshold_ <= hi) {
        const float y = yOf(threshold_);
        gg->allocate(6);
        auto* v = gg->vertexDataAsPoint2D();
        v[0].set(0, y - 0.5F);
        v[1].set(w, y - 0.5F);
        v[2].set(w, y + 0.5F);
        v[3].set(0, y - 0.5F);
        v[4].set(w, y + 0.5F);
        v[5].set(0, y + 0.5F);
    } else {
        gg->allocate(0);
    }
    guide->markDirty(QSGNode::DirtyGeometry);
    return root;
}

} // namespace rvn
