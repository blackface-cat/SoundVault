#pragma once

#include <QWidget>
#include <QVector>
#include <QString>
#include <QImage>
#include <QPointF>
#include <functional>

#include "ui/Theme.h"

/**
 * 波形/频谱组件（v1.3）：Audition 风格。
 *  - 波形模式：对称填充波形；立体声（有独立左右峰值）双轨显示，单声道单轨
 *  - 频谱模式：Audition 式 spectrogram（热度色映射，纵轴频率低→高）
 *  - 播放头 + 定位拖拽；选区模式（框选段落可导出拖入宿主）
 */
class WaveformWidget : public QWidget
{
    Q_OBJECT

public:
    enum class Mode { Waveform, Spectrogram };

    explicit WaveformWidget(QWidget *parent = nullptr);

    /// 设置峰值：coarse 粗 / fine 细 / fineL·fineR 左右声道（立体声才有，为空=单声道）
    void setPeaks(const QVector<float> &coarse, const QVector<float> &fine = {},
                  const QVector<float> &fineL = {}, const QVector<float> &fineR = {});
    void setSpectrogram(const QImage &spec);
    void setMode(Mode m);
    Mode mode() const { return mode_; }
    void setThemeColors(const ThemeColors &c) { theme_ = c; update(); }
    void setPlayPosition(double pos) { playPos_ = pos; update(); }
    void setEmptyHint(const QString &hint) { emptyHint_ = hint; update(); }
    /// 音频时长（秒），用于把选区比例换算成时间
    void setDuration(double sec) { duration_ = sec; }

    bool hasRealPeaks() const { return realPeaks_.size() > 1; }
    bool hasSpectrogram() const { return !spec_.isNull(); }
    bool isStereo() const { return fineL_.size() > 1 && fineR_.size() > 1; }

    // ---- 选区（框选段落导出） ----
    void setSelectMode(bool on);             // 开启后拖拽=框选，而非定位
    bool selectMode() const { return selectMode_; }
    void clearSelection();
    bool hasSelection() const { return selT0_ >= 0 && selT1_ > selT0_; }
    double selectionStart() const { return selT0_; }
    double selectionEnd() const { return selT1_; }
    /// 导出回调：入参 (起始秒, 结束秒)，返回导出文件绝对路径（空=失败）
    void setExporter(std::function<QString(double, double)> fn) { exporter_ = std::move(fn); }

signals:
    void seekRequested(double ratio);
    /// 选区变化（起始秒, 结束秒）；两者为 -1 表示清除
    void selectionChanged(double t0, double t1);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void paintWaveform(QPainter &p, const QRectF &r);
    void paintSpectrogram(QPainter &p, const QRectF &r);
    void paintSelection(QPainter &p, const QRectF &r);
    void updatePlayFromMouse(const QPointF &pos);
    double ratioFromX(const QPointF &pos) const;
    void rebuildRenderCache();

    Mode mode_ = Mode::Waveform;
    QVector<float> realPeaks_;     // 细峰值（混合，单声道渲染用）
    QVector<float> fineL_;         // 左声道细峰值
    QVector<float> fineR_;         // 右声道细峰值
    QVector<float> renderCache_;   // 混合渲染缓存
    QVector<float> renderL_;       // 左声道渲染缓存
    QVector<float> renderR_;       // 右声道渲染缓存
    int renderBins_ = 0;
    QImage spec_;
    double playPos_ = 0;
    double duration_ = 0;
    bool seeking_ = false;

    // 选区状态
    bool selectMode_ = false;
    double selT0_ = -1, selT1_ = -1;   // 秒（-1 表示无）
    double selRatio0_ = 0, selRatio1_ = 0; // 比例（渲染用）
    bool dragging_ = false;            // 正在框选
    bool exportDragging_ = false;      // 正在从已有选区拖出导出
    bool exportStarted_ = false;
    QPointF dragStart_;
    std::function<QString(double, double)> exporter_;

    ThemeColors theme_;
    QString emptyHint_ = QStringLiteral("在列表中选择素材即可试听 · 点击波形定位播放");
};
