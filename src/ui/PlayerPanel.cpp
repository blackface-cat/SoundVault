#include "ui/PlayerPanel.h"
#include "ui/WaveformWidget.h"
#include "ui/Icons.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QToolButton>
#include <QProgressBar>
#include <QFrame>
#include <QDesktopServices>
#include <QFileInfo>
#include <QUrl>
#include <QtMath>

namespace {
QString fmtClock(double sec)
{
    const int s = int(sec);
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

QToolButton *iconBtn(QWidget *parent, const QString &objName, QSize iconSize)
{
    auto *b = new QToolButton(parent);
    b->setObjectName(objName);
    b->setIconSize(iconSize);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}
}

PlayerPanel::PlayerPanel(QWidget *parent)
    : QWidget(parent)
    , theme_(Theme::colors(false))
{
    setObjectName(QStringLiteral("PlayerBar"));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // ============ 波形/频谱大区 ============
    body_ = new QWidget(this);
    auto *bl = new QVBoxLayout(body_);
    bl->setContentsMargins(10, 6, 10, 2);
    bl->setSpacing(3);

    // 顶部：视图切换 + 电平表
    auto *head = new QHBoxLayout();
    waveBtn_ = iconBtn(body_, QStringLiteral("IconBtn"), QSize(15, 15));
    waveBtn_->setCheckable(true);
    waveBtn_->setChecked(true);
    waveBtn_->setToolTip(QStringLiteral("波形视图"));
    connect(waveBtn_, &QToolButton::clicked, this, [this] {
        waveform_->setMode(WaveformWidget::Mode::Waveform);
        waveBtn_->setChecked(true);
        specBtn_->setChecked(false);
    });
    head->addWidget(waveBtn_);

    specBtn_ = iconBtn(body_, QStringLiteral("IconBtn"), QSize(15, 15));
    specBtn_->setCheckable(true);
    specBtn_->setToolTip(QStringLiteral("频谱图视图"));
    connect(specBtn_, &QToolButton::clicked, this, [this] {
        waveform_->setMode(WaveformWidget::Mode::Spectrogram);
        specBtn_->setChecked(true);
        waveBtn_->setChecked(false);
    });
    head->addWidget(specBtn_);

    // 选区框选（导出拖入宿主）
    selectBtn_ = iconBtn(body_, QStringLiteral("IconBtn"), QSize(15, 15));
    selectBtn_->setCheckable(true);
    selectBtn_->setToolTip(QStringLiteral("框选段落（开启后拖拽波形框选，选中段落可直接拖出到宿主软件）"));
    connect(selectBtn_, &QToolButton::toggled, this, [this](bool on) {
        if (waveform_)
            waveform_->setSelectMode(on);
        updateSelectUi();
    });
    head->addWidget(selectBtn_);

    exportBtn_ = iconBtn(body_, QStringLiteral("IconBtn"), QSize(15, 15));
    exportBtn_->setEnabled(false);
    exportBtn_->setToolTip(QStringLiteral("导出选区到文件"));
    connect(exportBtn_, &QToolButton::clicked, this, [this] {
        if (!waveform_ || !waveform_->hasSelection())
            return;
        const QString out = segmentExporter_
            ? segmentExporter_(waveform_->selectionStart(), waveform_->selectionEnd()) : QString();
        if (!out.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(out).absolutePath()));
    });
    head->addWidget(exportBtn_);

    head->addStretch(1);

    // 立体声电平表（跟随播放位置的峰值）
    auto *lvLbl = new QLabel(QStringLiteral("L"), body_);
    lvLbl->setObjectName(QStringLiteral("VolLabel"));
    head->addWidget(lvLbl);
    levelL_ = new QProgressBar(body_);
    levelL_->setRange(0, 100);
    levelL_->setValue(0);
    levelL_->setTextVisible(false);
    levelL_->setFixedSize(54, 6);
    head->addWidget(levelL_);
    auto *rvLbl = new QLabel(QStringLiteral("R"), body_);
    rvLbl->setObjectName(QStringLiteral("VolLabel"));
    head->addWidget(rvLbl);
    levelR_ = new QProgressBar(body_);
    levelR_->setRange(0, 100);
    levelR_->setValue(0);
    levelR_->setTextVisible(false);
    levelR_->setFixedSize(54, 6);
    head->addWidget(levelR_);

    head->addSpacing(8);
    bl->addLayout(head);

    waveform_ = new WaveformWidget(body_);
    waveform_->setMinimumHeight(132);
    bl->addWidget(waveform_, 1);
    connect(waveform_, &WaveformWidget::seekRequested, this, &PlayerPanel::seekRequested);
    connect(waveform_, &WaveformWidget::selectionChanged, this, [this](double, double) {
        updateSelectUi();
    });

    outer->addWidget(body_, 1);

    auto *hline = new QFrame(this);
    hline->setObjectName(QStringLiteral("HLine"));
    hline->setFrameShape(QFrame::NoFrame);
    hline->setFixedHeight(1);
    outer->addWidget(hline);

    // ============ 走带区 ============
    auto *ctrl = new QWidget(this);
    auto *h = new QHBoxLayout(ctrl);
    h->setContentsMargins(12, 5, 12, 7);
    h->setSpacing(8);

    // 左：曲目信息
    auto *infoWrap = new QVBoxLayout();
    infoWrap->setSpacing(1);
    fileLabel_ = new QLabel(QStringLiteral("未载入音频"), ctrl);
    fileLabel_->setObjectName(QStringLiteral("NowPlaying"));
    fileLabel_->setMinimumWidth(220);
    infoWrap->addWidget(fileLabel_);
    metaLabel_ = new QLabel(ctrl);
    metaLabel_->setObjectName(QStringLiteral("AppSub"));
    metaLabel_->setStyleSheet(QStringLiteral("font-size:11px;"));
    infoWrap->addWidget(metaLabel_);
    h->addLayout(infoWrap, 1);

    // 中：时间码 + 走带
    timeLabel_ = new QLabel(QStringLiteral("0:00 / 0:00"), ctrl);
    timeLabel_->setObjectName(QStringLiteral("TimeLabel"));
    h->addWidget(timeLabel_);

    h->addSpacing(6);

    prevBtn_ = iconBtn(ctrl, QStringLiteral("IconBtn"), QSize(16, 16));
    prevBtn_->setEnabled(false);
    prevBtn_->setToolTip(QStringLiteral("上一个"));
    connect(prevBtn_, &QToolButton::clicked, this, &PlayerPanel::prevRequested);
    h->addWidget(prevBtn_);

    playBtn_ = iconBtn(ctrl, QStringLiteral("PlayBtn"), QSize(26, 26));
    playBtn_->setEnabled(false);
    playBtn_->setToolTip(QStringLiteral("播放 / 暂停（空格）"));
    connect(playBtn_, &QToolButton::clicked, this, [this] {
        if (playing_)
            emit pauseRequested();
        else
            emit playRequested();
    });
    h->addWidget(playBtn_);

    stopBtn_ = iconBtn(ctrl, QStringLiteral("IconBtn"), QSize(15, 15));
    stopBtn_->setEnabled(false);
    stopBtn_->setToolTip(QStringLiteral("停止"));
    connect(stopBtn_, &QToolButton::clicked, this, &PlayerPanel::stopRequested);
    h->addWidget(stopBtn_);

    nextBtn_ = iconBtn(ctrl, QStringLiteral("IconBtn"), QSize(16, 16));
    nextBtn_->setEnabled(false);
    nextBtn_->setToolTip(QStringLiteral("下一个"));
    connect(nextBtn_, &QToolButton::clicked, this, &PlayerPanel::nextRequested);
    h->addWidget(nextBtn_);

    loopBtn_ = iconBtn(ctrl, QStringLiteral("IconBtn"), QSize(15, 15));
    loopBtn_->setCheckable(true);
    loopBtn_->setToolTip(QStringLiteral("循环播放"));
    connect(loopBtn_, &QToolButton::toggled, this, [this](bool on) {
        loop_ = on;
        emit loopToggled(on);
    });
    h->addWidget(loopBtn_);

    contBtn_ = iconBtn(ctrl, QStringLiteral("IconBtn"), QSize(15, 15));
    contBtn_->setCheckable(true);
    contBtn_->setChecked(true);
    contBtn_->setToolTip(QStringLiteral("连续播放（播完自动下一个）"));
    connect(contBtn_, &QToolButton::toggled, this, [this](bool on) {
        cont_ = on;
        emit contToggled(on);
    });
    h->addWidget(contBtn_);

    h->addSpacing(10);

    // 右：速度 / 音量
    auto *speedCap = new QLabel(QStringLiteral("速度"), ctrl);
    speedCap->setObjectName(QStringLiteral("VolLabel"));
    h->addWidget(speedCap);
    speedValue_ = new QLabel(QStringLiteral("1.0×"), ctrl);
    speedValue_->setObjectName(QStringLiteral("TimeLabel"));
    speedValue_->setFixedWidth(32);
    speedValue_->setAlignment(Qt::AlignCenter);
    h->addWidget(speedValue_);
    speed_ = new QSlider(Qt::Horizontal, ctrl);
    speed_->setRange(50, 200);
    speed_->setValue(100);
    speed_->setFixedWidth(84);
    speed_->setToolTip(QStringLiteral("0.5×–2.0× 变速（只影响试听，不改文件）"));
    connect(speed_, &QSlider::valueChanged, this, [this](int v) {
        speedValue_->setText(QStringLiteral("%1×").arg(v / 100.0, 0, 'f', 1));
        emit speedChanged(v / 100.0);
    });
    h->addWidget(speed_);

    h->addSpacing(6);

    auto *volCap = new QLabel(QStringLiteral("音量"), ctrl);
    volCap->setObjectName(QStringLiteral("VolLabel"));
    h->addWidget(volCap);
    volume_ = new QSlider(Qt::Horizontal, ctrl);
    volume_->setRange(0, 100);
    volume_->setValue(70);
    volume_->setFixedWidth(84);
    volume_->setToolTip(QStringLiteral("音量"));
    connect(volume_, &QSlider::valueChanged, this,
            [this](int v) { emit volumeChanged(v / 100.0); });
    h->addWidget(volume_);

    // 折叠/展开波形区按钮（放在走带区，折叠后仍然可见可点，修复"折叠后找不到"）
    h->addSpacing(6);
    collapseBtn_ = iconBtn(ctrl, QStringLiteral("IconBtn"), QSize(15, 15));
    collapseBtn_->setCheckable(true);
    collapseBtn_->setToolTip(QStringLiteral("折叠/展开波形区"));
    connect(collapseBtn_, &QToolButton::toggled, this, [this](bool collapsed) {
        if (body_)
            body_->setVisible(!collapsed);
        updateIcons();
    });
    h->addWidget(collapseBtn_);

    outer->addWidget(ctrl);

    updateIcons();
}

void PlayerPanel::updateIcons()
{
    const QColor ic = theme_.text;
    playBtn_->setIcon(Icons::make(playing_ ? Icons::Path::Pause : Icons::Path::Play, ic));
    stopBtn_->setIcon(Icons::make(Icons::Path::Stop, theme_.muted));
    prevBtn_->setIcon(Icons::make(Icons::Path::Prev, theme_.muted));
    nextBtn_->setIcon(Icons::make(Icons::Path::Next, theme_.muted));
    loopBtn_->setIcon(Icons::make(Icons::Path::Repeat, loop_ ? theme_.accent : theme_.muted));
    contBtn_->setIcon(Icons::make(Icons::Path::List, cont_ ? theme_.accent : theme_.muted));
    collapseBtn_->setIcon(Icons::make(
        collapseBtn_->isChecked() ? Icons::Path::ChevronDown : Icons::Path::ChevronUp,
        theme_.muted));
    waveBtn_->setIcon(Icons::make(Icons::Path::Wave,
        waveform_ && waveform_->mode() == WaveformWidget::Mode::Waveform ? theme_.accent : theme_.muted));
    specBtn_->setIcon(Icons::make(Icons::Path::Spectrum,
        waveform_ && waveform_->mode() == WaveformWidget::Mode::Spectrogram ? theme_.accent : theme_.muted));
    selectBtn_->setIcon(Icons::make(Icons::Path::Scissors,
        selectBtn_->isChecked() ? theme_.accent : theme_.muted));
    exportBtn_->setIcon(Icons::make(Icons::Path::Download,
        exportBtn_->isEnabled() ? theme_.accent : theme_.muted));
}

void PlayerPanel::updateLevels()
{
    // 用波形峰值在播放位置附近的数值做电平指示（跟随播放跳动）
    if (!levelL_ || !levelR_ || peaks_.isEmpty()) {
        if (levelL_) levelL_->setValue(0);
        if (levelR_) levelR_->setValue(0);
        return;
    }
    const auto avgNear = [this](const QVector<float> &arr) {
        const int n = arr.size();
        const int idx = qBound(0, int(playRatio_ * (n - 1)), n - 1);
        const int w = qMax(1, n / 60);
        double sum = 0;
        int cnt = 0;
        for (int i = qMax(0, idx - w); i <= qMin(n - 1, idx + w); ++i) {
            sum += arr.at(i);
            ++cnt;
        }
        return cnt ? sum / cnt : 0.0;
    };
    // 有独立左右声道峰值时，L/R 分别指示；否则单声道镜像
    if (peaksL_.size() > 1 && peaksR_.size() > 1) {
        levelL_->setValue(qBound(0, int(avgNear(peaksL_) * 100), 100));
        levelR_->setValue(qBound(0, int(avgNear(peaksR_) * 100), 100));
    } else {
        const int lv = qBound(0, int(avgNear(peaks_) * 100), 100);
        levelL_->setValue(lv);
        levelR_->setValue(lv);
    }
}

void PlayerPanel::setTrackInfo(const QString &name, const QString &meta)
{
    if (name.isEmpty()) {
        fileLabel_->setText(QStringLiteral("未载入音频"));
        metaLabel_->clear();
        return;
    }
    fileLabel_->setText(name);
    fileLabel_->setToolTip(name);
    metaLabel_->setText(meta);
}

void PlayerPanel::setPeaks(const QVector<float> &coarse, const QVector<float> &fine,
                           const QVector<float> &fineL, const QVector<float> &fineR)
{
    peaks_ = fine.size() > 1 ? fine : coarse;
    peaksL_ = fineL;
    peaksR_ = fineR;
    waveform_->setPeaks(coarse, fine, fineL, fineR);
}

void PlayerPanel::setSpectrogram(const QImage &spec)
{
    waveform_->setSpectrogram(spec);
}

void PlayerPanel::setSegmentExporter(std::function<QString(double, double)> fn)
{
    segmentExporter_ = std::move(fn);
    if (waveform_)
        waveform_->setExporter(segmentExporter_);
}

void PlayerPanel::updateSelectUi()
{
    if (!exportBtn_ || !waveform_)
        return;
    exportBtn_->setEnabled(waveform_->hasSelection());
    updateIcons();
}

void PlayerPanel::setPlayPosition(double ratio)
{
    playRatio_ = ratio;
    waveform_->setPlayPosition(ratio);
    const double pos = ratio * duration_;
    timeLabel_->setText(QStringLiteral("%1 / %2").arg(fmtClock(pos), fmtClock(duration_)));
    if (playing_)
        updateLevels();
}

void PlayerPanel::setState(bool playing, bool hasFile)
{
    playing_ = playing;
    playBtn_->setEnabled(hasFile);
    stopBtn_->setEnabled(hasFile);
    prevBtn_->setEnabled(hasFile);
    nextBtn_->setEnabled(hasFile);
    if (!playing) {
        if (levelL_) levelL_->setValue(0);
        if (levelR_) levelR_->setValue(0);
    }
    updateIcons();
}

void PlayerPanel::setDuration(double sec)
{
    duration_ = sec;
    if (waveform_)
        waveform_->setDuration(sec);
}

void PlayerPanel::setThemeColors(const ThemeColors &c)
{
    theme_ = c;
    if (waveform_)
        waveform_->setThemeColors(c);
    updateIcons();
}

void PlayerPanel::setVolumeUi(int percent)
{
    volume_->blockSignals(true);
    volume_->setValue(qBound(0, percent, 100));
    volume_->blockSignals(false);
}

void PlayerPanel::setSpeedUi(int percent)
{
    speed_->blockSignals(true);
    speed_->setValue(qBound(50, percent, 200));
    speed_->blockSignals(false);
    speedValue_->setText(QStringLiteral("%1×").arg(speed_->value() / 100.0, 0, 'f', 1));
}
