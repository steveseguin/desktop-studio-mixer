#pragma once

#include <QElapsedTimer>
#include <QPainter>
#include <QProgressBar>
#include <algorithm>
#include <cmath>

namespace versus::mixer {
// Retain the accessible progress-bar role/value, expressed in dBFS rather than
// linear amplitude. Painting is updated only when the visible reading changes.
class AudioLevelMeter : public QProgressBar {
public:
    explicit AudioLevelMeter(QWidget *parent = nullptr) : QProgressBar(parent) {
        setRange(-60, 0); setValue(-60); setTextVisible(false);
        setMinimumWidth(200); setFixedHeight(58);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setToolTip("Peak level after volume and mute, in dBFS. White marker holds the peak for one second. "
                   "Green below -18 dBFS, amber above -18, red above -6. CLIP indicates a level at or above 0 dBFS.");
        clock_.start();
    }

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
        if (clipped != clipped_) setAccessibleDescription(clipped ? "Clipping. Reduce this track's volume." : "Peak audio level in dBFS, from -60 to 0.");
        db_ = db; clipped_ = clipped;
        setValue(qRound(db));
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
} // namespace versus::mixer
