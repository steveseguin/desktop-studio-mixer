#pragma once

#include <QElapsedTimer>
#include <QPainter>
#include <QWidget>
#include <QAccessibleWidget>
#include <QAccessibleValueInterface>
#include <algorithm>
#include <cmath>

namespace versus::mixer {
inline QAccessibleInterface *audioMeterAccessibility(const QString &, QObject *);
// A live level is an indicator, not task progress. Expose its value on demand
// without sending a stream of progress events that makes screen readers beep.
class AudioLevelMeter : public QWidget {
public:
    explicit AudioLevelMeter(QWidget *parent = nullptr) : QWidget(parent) {
        static const bool installed = [] { QAccessible::installFactory(audioMeterAccessibility); return true; }();
        (void)installed;
        setMinimumWidth(200); setFixedHeight(58);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setToolTip("Peak level after volume and mute, in dBFS. White marker holds the peak for one second. "
                   "Green below -18 dBFS, amber above -18, red above -6. CLIP indicates a level at or above 0 dBFS.");
        clock_.start();
        setAccessibleDescription("Peak audio level in dBFS, from -60 to 0. Use Ctrl+Shift+L to read the selected track or master level.");
    }

    double level() const { return db_; }
    QString reading() const { return clipped_ ? "Clipping" : db_ <= -60 ? "Silent, below -60 dBFS" : QString::number(db_, 'f', 1)+" dBFS"; }

    void setLevel(float amplitude) {
        const auto now = clock_.elapsed();
        const auto elapsed = std::min<qint64>(now-lastUpdate_, 100); lastUpdate_ = now;
        const float db = std::isfinite(amplitude) && amplitude > .001f
            ? std::clamp(20.0f*std::log10(amplitude), -60.0f, 0.0f) : -60.0f;
        const float previousHeld = heldDb_;
        if (db >= heldDb_) { heldDb_ = db; holdUntil_ = now+1000; }
        else if (now > holdUntil_) heldDb_ = std::max(db, heldDb_-24.0f*elapsed/1000.0f);
        if (std::isfinite(amplitude) && amplitude >= 1.0f) clipUntil_ = now+2000;
        const bool clipped = now < clipUntil_;
        const bool changed = qRound(db*10) != qRound(db_*10) || qRound(previousHeld*10) != qRound(heldDb_*10) || clipped != clipped_;
        db_ = db; clipped_ = clipped;
        if (changed) update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor("#0d1721"));
        QFont small = font(); small.setPixelSize(10); painter.setFont(small);
        painter.setPen(QColor("#9bb0c4"));
        painter.drawText(QRectF(7, 2, width()-14, 14), Qt::AlignLeft | Qt::AlignVCenter, "LEVEL");
        painter.setPen(clipped_ ? QColor("#ff6574") : QColor("#eaf4ff"));
        const QString reading = clipped_ ? "CLIP" : (db_ <= -60 ? "-inf dBFS" : QString::number(db_, 'f', 1)+" dBFS");
        painter.drawText(QRectF(7, 2, width()-14, 14), Qt::AlignRight | Qt::AlignVCenter, reading);
        const QRectF bar(7, 20, width()-14, 14);
        constexpr int segments = 60;
        for (int i = 0; i < segments; ++i) {
            const float threshold = -60.0f+i;
            const QColor color = threshold >= -6 ? QColor("#ff596b") : threshold >= -18 ? QColor("#efbd50") : QColor("#39d99a");
            painter.fillRect(QRectF(bar.x()+i*bar.width()/segments, bar.y(), std::max(1.0, bar.width()/segments-1), bar.height()),
                             db_ > threshold ? color : QColor("#213244"));
        }
        if (heldDb_ > -60) {
            const double marker = bar.left()+(heldDb_+60)/60*bar.width();
            painter.fillRect(QRectF(std::min(marker, bar.right()-2),bar.top()-1,2,bar.height()+2),QColor("#f2f7fc"));
        }
        painter.setPen(QColor("#9bb0c4"));
        for (int tick : {-60, -40, -20, -6, 0}) {
            const double at = bar.left()+(tick+60)/60.0*bar.width();
            painter.drawLine(QPointF(at,bar.bottom()+2),QPointF(at,bar.bottom()+5));
            const double labelX = std::clamp(at-12, bar.left(), bar.right()-24);
            painter.drawText(QRectF(labelX,bar.bottom()+6,24,12),Qt::AlignCenter,QString::number(tick));
        }
    }

private:
    QElapsedTimer clock_;
    qint64 lastUpdate_ = 0, holdUntil_ = 0, clipUntil_ = 0;
    float db_ = -60, heldDb_ = -60;
    bool clipped_ = false;
};

class AudioMeterAccessible final : public QAccessibleWidget, public QAccessibleValueInterface {
    AudioLevelMeter *meter() const { return static_cast<AudioLevelMeter*>(widget()); }
public:
    explicit AudioMeterAccessible(AudioLevelMeter *meter) : QAccessibleWidget(meter, QAccessible::Indicator) {}
    QString text(QAccessible::Text kind) const override {
        if (kind == QAccessible::Name) return QAccessibleWidget::text(kind)+": "+meter()->reading();
        if (kind == QAccessible::Value) return meter()->reading();
        return QAccessibleWidget::text(kind);
    }
    void *interface_cast(QAccessible::InterfaceType type) override {
        if (type == QAccessible::ValueInterface) return static_cast<QAccessibleValueInterface*>(this);
        return QAccessibleWidget::interface_cast(type);
    }
    QVariant currentValue() const override { return meter()->level(); }
    QVariant minimumValue() const override { return -60.0; }
    QVariant maximumValue() const override { return 0.0; }
    QVariant minimumStepSize() const override { return 0.1; }
    void setCurrentValue(const QVariant &) override {} // A read-only indicator.
    QAccessible::State state() const override { auto s = QAccessibleWidget::state(); s.readOnly = true; return s; }
};
inline QAccessibleInterface *audioMeterAccessibility(const QString &, QObject *object) {
    if (auto *meter = dynamic_cast<AudioLevelMeter*>(object)) return new AudioMeterAccessible(meter);
    return nullptr;
}
} // namespace versus::mixer
