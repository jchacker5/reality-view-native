#pragma once

#include <QColor>
#include <QList>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

namespace rvn {

/// Lightweight time-series chart drawn with two scene-graph geometry nodes (filled area +
/// line ribbon). Canvas would rasterize on the CPU every update; this costs one small vertex
/// upload per data change.
class SparklineItem : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QList<float> values READ values WRITE setValues NOTIFY valuesChanged)
    Q_PROPERTY(double minValue MEMBER minValue_ NOTIFY rangeChanged)
    Q_PROPERTY(double maxValue MEMBER maxValue_ NOTIFY rangeChanged)
    Q_PROPERTY(bool autoRange MEMBER autoRange_ NOTIFY rangeChanged)
    Q_PROPERTY(double threshold MEMBER threshold_ NOTIFY rangeChanged) ///< draws a guide line; NaN = none
    Q_PROPERTY(QColor color MEMBER color_ NOTIFY colorChanged)
    Q_PROPERTY(QColor thresholdColor MEMBER thresholdColor_ NOTIFY colorChanged)

public:
    explicit SparklineItem(QQuickItem* parent = nullptr);

    [[nodiscard]] QList<float> values() const { return values_; }
    void setValues(const QList<float>& values);

signals:
    void valuesChanged();
    void rangeChanged();
    void colorChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;

private:
    QList<float> values_;
    double minValue_{0.0};
    double maxValue_{1.0};
    bool autoRange_{true};
    double threshold_{qQNaN()};
    QColor color_{QStringLiteral("#3d7bff")};
    QColor thresholdColor_{QStringLiteral("#ffb020")};
};

} // namespace rvn
