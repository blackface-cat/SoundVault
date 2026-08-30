#pragma once

#include <QWidget>
#include <QVector>
#include <QImage>
#include <functional>

#include "ui/Theme.h"

class QLabel;
class QSlider;
class QToolButton;
class QProgressBar;
class WaveformWidget;

/**
 * 底部播放条（v1.3 · 参考 MATRIX WAVE 底部工作台）
 *
 *   [波形/频谱大区]  顶部：波形·频谱切换 + 选区框选 + 立体声电平表
 *   [走带区]         曲目信息  |  时间码 + 上一首/播放(大)/停止/下一首/循环/连续  |  速度·音量·折叠
 */
class PlayerPanel : public QWidget
{
    Q_OBJECT

public:
    explicit PlayerPanel(QWidget *parent = nullptr);

    void setPeaks(const QVector<float> &coarse, const QVector<float> &fine = {},
                  const QVector<float> &fineL = {}, const QVector<float> &fineR = {});
    void setSpectrogram(const QImage &spec);
    void setPlayPosition(double ratio);
    void setState(bool playing, bool hasFile);
    void setDuration(double sec);
    void setTrackInfo(const QString &name, const QString &meta);
    void setThemeColors(const ThemeColors &c);
    /// 选区导出回调：(起始秒, 结束秒) → 导出文件绝对路径（空=失败）
    void setSegmentExporter(std::function<QString(double, double)> fn);

    void setVolumeUi(int percent);
    void setSpeedUi(int percent);

signals:
    void playRequested();
    void pauseRequested();
    void stopRequested();
    void seekRequested(double ratio);
    void volumeChanged(double v);
    void speedChanged(double v);
    void prevRequested();
    void nextRequested();
    void loopToggled(bool enabled);
    void contToggled(bool enabled);

private:
    void updateIcons();
    void updateLevels();
    void updateSelectUi();

    WaveformWidget *waveform_ = nullptr;
    QToolButton *waveBtn_ = nullptr;
    QToolButton *specBtn_ = nullptr;
    QToolButton *selectBtn_ = nullptr;
    QToolButton *exportBtn_ = nullptr;
    QToolButton *collapseBtn_ = nullptr;
    QProgressBar *levelL_ = nullptr;
    QProgressBar *levelR_ = nullptr;

    QToolButton *prevBtn_ = nullptr;
    QToolButton *playBtn_ = nullptr;
    QToolButton *stopBtn_ = nullptr;
    QToolButton *nextBtn_ = nullptr;
    QToolButton *loopBtn_ = nullptr;
    QToolButton *contBtn_ = nullptr;

    QLabel *timeLabel_ = nullptr;
    QLabel *fileLabel_ = nullptr;
    QLabel *metaLabel_ = nullptr;
    QLabel *speedValue_ = nullptr;
    QSlider *volume_ = nullptr;
    QSlider *speed_ = nullptr;
    QWidget *body_ = nullptr;

    QVector<float> peaks_;      // 用于电平指示（混合）
    QVector<float> peaksL_;     // 左声道峰值（立体声才有）
    QVector<float> peaksR_;     // 右声道峰值（立体声才有）
    double duration_ = 0;
    double playRatio_ = 0;
    bool playing_ = false;
    bool loop_ = false;
    bool cont_ = true;
    ThemeColors theme_;
    std::function<QString(double, double)> segmentExporter_;
};
