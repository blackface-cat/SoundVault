#include "ui/WaveformWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QLinearGradient>
#include <QDrag>
#include <QMimeData>
#include <QUrl>
#include <QApplication>
#include <QtMath>

WaveformWidget::WaveformWidget(QWidget *parent)
    : QWidget(parent)
    , theme_(Theme::colors(false))
{
    setMinimumHeight(96);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
}

void WaveformWidget::setPeaks(const QVector<float> &coarse, const QVector<float> &fine,
                              const QVector<float> &fineL, const QVector<float> &fineR)
{
    // 优先用细峰值做渲染（Audition 需要细节）
    realPeaks_ = fine.size() > 1 ? fine : coarse;
    fineL_ = fineL;
    fineR_ = fineR;
    renderCache_.clear();
    renderL_.clear();
    renderR_.clear();
    renderBins_ = 0;
    if (!realPeaks_.isEmpty())
        playPos_ = 0;
    update();
}

void WaveformWidget::setSpectrogram(const QImage &spec)
{
    spec_ = spec;
    update();
}

void WaveformWidget::setMode(Mode m)
{
    mode_ = m;
    update();
}

void WaveformWidget::setSelectMode(bool on)
{
    selectMode_ = on;
    if (!on) {
        clearSelection();
        setCursor(Qt::ArrowCursor);
    } else {
        setCursor(Qt::IBeamCursor);
    }
    update();
}

void WaveformWidget::clearSelection()
{
    selT0_ = selT1_ = -1;
    selRatio0_ = selRatio1_ = 0;
    update();
    emit selectionChanged(-1, -1);
}

void WaveformWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    renderCache_.clear();
    renderL_.clear();
    renderR_.clear();
    renderBins_ = 0;
}

double WaveformWidget::ratioFromX(const QPointF &pos) const
{
    return qBound(0.0, (pos.x() - 1.0) / qMax(1.0, width() - 2.0), 1.0);
}

void WaveformWidget::rebuildRenderCache()
{
    const int n = int(width());
    if (n <= 0)
        return;
    if (renderBins_ == n)
        return;
    const auto build = [&](const QVector<float> &src) {
        QVector<float> out;
        out.resize(n);
        const int m = src.size();
        if (m < 2) {
            out.fill(0.f);
            return out;
        }
        for (int i = 0; i < n; ++i) {
            const double x = double(i) * (m - 1) / (n - 1);
            const int i0 = int(x);
            const int i1 = qMin(i0 + 1, m - 1);
            const double frac = x - i0;
            out[i] = float(src[i0] * (1.0 - frac) + src[i1] * frac);
        }
        return out;
    };
    renderCache_ = build(realPeaks_);
    if (isStereo()) {
        renderL_ = build(fineL_);
        renderR_ = build(fineR_);
    }
    renderBins_ = n;
}

// ---------------------------------------------------------------- 波形绘制

void WaveformWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const QRectF r = rect().adjusted(1, 1, -1, -1);
    p.fillRect(r, theme_.waveformBg);

    if (mode_ == Mode::Spectrogram) {
        if (!hasSpectrogram()) {
            p.setPen(theme_.muted);
            p.drawText(r, Qt::AlignCenter, QStringLiteral("频谱生成中…"));
            return;
        }
        paintSpectrogram(p, r);
        const double px = r.left() + r.width() * playPos_;
        p.setPen(QPen(QColor(255, 255, 255, 200), 1));
        p.drawLine(QPointF(px, r.top()), QPointF(px, r.bottom()));
        return;
    }

    // 波形模式
    if (!hasRealPeaks()) {
        p.setPen(QPen(theme_.border, 1));
        p.drawLine(QPointF(r.left(), r.center().y()), QPointF(r.right(), r.center().y()));
        p.setPen(theme_.muted);
        p.drawText(r, Qt::AlignCenter, emptyHint_);
        return;
    }
    paintWaveform(p, r);
    if (selectMode_ || hasSelection())
        paintSelection(p, r);
}

void WaveformWidget::paintWaveform(QPainter &p, const QRectF &r)
{
    const int n = int(r.width());
    if (n <= 0)
        return;
    rebuildRenderCache();

    const double playedX = r.left() + r.width() * playPos_;

    // 单条轨道的对称填充（中心 cy，上下 laneH 振幅）
    const auto drawLane = [&](const QVector<float> &cache, double cy, double laneH,
                              const QColor &line, const QColor &fill) {
        QPainterPath path;
        path.moveTo(r.left(), cy);
        for (int i = 0; i < n; ++i) {
            const double x = r.left() + double(i);
            const double v = qBound(0.0, double(cache[i]), 1.0);
            path.lineTo(x, cy - v * laneH);
        }
        for (int i = n - 1; i >= 0; --i) {
            const double x = r.left() + double(i);
            const double v = qBound(0.0, double(cache[i]), 1.0);
            path.lineTo(x, cy + v * laneH);
        }
        path.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawPath(path);
        p.setPen(QPen(line, 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    };

    const auto drawTrack = [&](const QVector<float> &cache, double cy, double laneH,
                               const QColor &unplayed, const QColor &played) {
        // 未播放部分
        drawLane(cache, cy, laneH, unplayed, unplayed);
        // 已播放部分（强调色覆盖）
        if (playedX > r.left()) {
            const QRectF clip(r.left(), r.top(), playedX - r.left(), r.height());
            p.save();
            p.setClipRect(clip);
            drawLane(cache, cy, laneH, played, played);
            p.restore();
        }
    };

    if (isStereo()) {
        // 双轨：上=左，下=右
        const double halfH = r.height() * 0.5;
        const double laneH = halfH * 0.5 - 3.0;
        const double cyL = r.top() + halfH * 0.5;
        const double cyR = r.top() + halfH * 1.5;
        drawTrack(renderL_, cyL, laneH, theme_.waveformLine, theme_.waveformPlayed);
        drawTrack(renderR_, cyR, laneH, theme_.waveformLine, theme_.waveformPlayed);
        // 双轨分隔线 + L/R 标记
        p.setPen(QPen(theme_.border, 1));
        p.drawLine(QPointF(r.left(), r.center().y()), QPointF(r.right(), r.center().y()));
        p.setPen(theme_.muted);
        QFont f = p.font();
        f.setPointSize(7);
        p.setFont(f);
        p.drawText(QPointF(r.left() + 4, cyL - laneH - 2), QStringLiteral("L"));
        p.drawText(QPointF(r.left() + 4, cyR - laneH - 2), QStringLiteral("R"));
    } else {
        const double cy = r.center().y();
        const double laneH = r.height() * 0.5 - 3.0;
        drawTrack(renderCache_, cy, laneH, theme_.waveformLine, theme_.waveformPlayed);
        // 中央基线
        p.setPen(QPen(theme_.border, 1));
        p.drawLine(QPointF(r.left(), cy), QPointF(r.right(), cy));
    }

    // 播放头
    QColor head = theme_.text;
    head.setAlpha(200);
    p.setPen(QPen(head, 1));
    p.drawLine(QPointF(playedX, r.top()), QPointF(playedX, r.bottom()));
}

void WaveformWidget::paintSelection(QPainter &p, const QRectF &r)
{
    if (!hasSelection())
        return;
    const double x0 = r.left() + r.width() * selRatio0_;
    const double x1 = r.left() + r.width() * selRatio1_;
    QColor fill = theme_.accent;
    fill.setAlpha(46);
    p.fillRect(QRectF(x0, r.top(), x1 - x0, r.height()), fill);
    p.setPen(QPen(theme_.accent, 1.5));
    p.drawLine(QPointF(x0, r.top()), QPointF(x0, r.bottom()));
    p.drawLine(QPointF(x1, r.top()), QPointF(x1, r.bottom()));
}

void WaveformWidget::paintSpectrogram(QPainter &p, const QRectF &r)
{
    if (spec_.isNull())
        return;
    const QImage scaled = spec_.scaled(int(r.width()), int(r.height()),
                                       Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    p.drawImage(r.topLeft(), scaled);
}

// ---------------------------------------------------------------- 交互

void WaveformWidget::updatePlayFromMouse(const QPointF &pos)
{
    const double ratio = ratioFromX(pos);
    playPos_ = ratio;
    update();
    emit seekRequested(ratio);
}

void WaveformWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    if (selectMode_) {
        const double ratio = ratioFromX(event->position());
        // 点击已有选区内部 → 准备拖出导出（拖进宿主）
        if (hasSelection()
            && ratio >= selRatio0_ - 0.004 && ratio <= selRatio1_ + 0.004) {
            exportDragging_ = true;
            exportStarted_ = false;
            dragStart_ = event->position();
            return;
        }
        // 否则开始新的框选
        dragging_ = true;
        selRatio0_ = selRatio1_ = ratio;
        selT0_ = selT1_ = ratio * duration_;
        update();
        return;
    }

    // 普通模式：点击定位
    seeking_ = true;
    updatePlayFromMouse(event->position());
    QWidget::mousePressEvent(event);
}

void WaveformWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (dragging_) {
        selRatio1_ = ratioFromX(event->position());
        selT0_ = qMin(selRatio0_, selRatio1_) * duration_;
        selT1_ = qMax(selRatio0_, selRatio1_) * duration_;
        update();
        return;
    }
    if (exportDragging_ && !exportStarted_) {
        if ((event->position() - dragStart_).manhattanLength()
            > QApplication::startDragDistance()) {
            exportStarted_ = true;
            const QString out = exporter_ ? exporter_(selT0_, selT1_) : QString();
            if (!out.isEmpty()) {
                auto *mime = new QMimeData;
                mime->setUrls({QUrl::fromLocalFile(out)});
                auto *drag = new QDrag(this);
                drag->setMimeData(mime);
                drag->exec(Qt::CopyAction);
            }
            exportDragging_ = false;
        }
        return;
    }
    if (seeking_)
        updatePlayFromMouse(event->position());
    QWidget::mouseMoveEvent(event);
}

void WaveformWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (dragging_) {
        dragging_ = false;
        const double t0 = qMin(selRatio0_, selRatio1_);
        const double t1 = qMax(selRatio0_, selRatio1_);
        // 过窄的框选视为取消
        if (t1 - t0 < 0.003) {
            clearSelection();
        } else {
            selRatio0_ = t0;
            selRatio1_ = t1;
            selT0_ = t0 * duration_;
            selT1_ = t1 * duration_;
            update();
            emit selectionChanged(selT0_, selT1_);
        }
        return;
    }
    if (exportDragging_) {
        exportDragging_ = false;
        exportStarted_ = false;
        return;
    }
    seeking_ = false;
    QWidget::mouseReleaseEvent(event);
}
