#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QByteArray>
#include <QVector>

#include <miniaudio/miniaudio.h>

/**
 * 播放引擎（v0.9 重写）
 *
 * 相比旧版的关键改进（参考 miniaudio 官方 engine_advanced 示例与
 * mackron/miniaudio#871 的设备切换结论）：
 *  1. 显式持有 ma_context，支持输出设备枚举 + 手动选择。
 *     （旧版永远用系统默认设备——直播机默认设备常是虚拟声卡，声音全部
 *       跑进 CABLE 导致"听不到声音"，这就是"声卡没办法正常使用"的主因。）
 *  2. 切换设备 = 整体重建 ma_engine（官方推荐做法），失败自动回退默认设备。
 *  3. 文件按 MA_SOUND_FLAG_STREAM 流式加载，GB 级 WAV 也能秒开不卡界面。
 *  4. 初始化/播放失败有明确 error() 文本，UI 可直接展示原因。
 */
class AudioEngine : public QObject
{
    Q_OBJECT
public:
    struct DeviceInfo {
        QByteArray id;      // ma_device_id 原始字节（用于持久化）
        QString name;
        bool isDefault = false;
    };

    enum class State { Idle, Playing, Paused };

    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine() override;

    // ---- 设备 ----
    QVector<DeviceInfo> outputDevices();             // 每次调用重新枚举
    bool selectDevice(const QByteArray &deviceId);   // 空 = 系统默认；失败自动回退
    QByteArray currentDeviceId() const { return currentDeviceId_; }
    QString currentDeviceName() const;
    bool isUsable() const { return engineReady_; }
    QString error() const { return error_; }

    // ---- 播放 ----
    bool loadFile(const QString &path);
    void play();
    void pause();
    void stop();
    void seekByRatio(double ratio);
    void seekBySecond(double sec);
    void setVolume(double v);          // 0.0 ~ 1.0
    void setSpeed(double speed);       // 0.5 ~ 2.0（pitch）
    void setLoop(bool on);

    State state() const { return state_; }
    double duration() const;
    double position() const;
    QString currentPath() const { return path_; }

signals:
    void stateChanged();
    void positionChanged(double seconds);
    void finished();                                   // 自然播完（循环关闭时）
    void loadFailed(const QString &path, const QString &reason);
    void deviceChanged();                              // 设备切换成功/失败后
    void deviceError(const QString &message);         // 设备不可用提示

private:
    bool rebuildEngine();          // teardown + init（用 currentDeviceId_）
    void teardownSound();
    void teardownEngine();
    void applyPlaybackParams();    // 重载声音后恢复音量/速度/循环

    ma_context context_{};
    bool contextReady_ = false;

    ma_engine engine_{};
    bool engineReady_ = false;

    ma_sound sound_{};
    bool hasSound_ = false;

    QByteArray currentDeviceId_;   // 空 = 系统默认
    QString currentDeviceName_;
    QString error_;

    State state_ = State::Idle;
    QString path_;
    double volume_ = 0.7;
    double speed_ = 1.0;
    bool loop_ = false;
    bool endEmitted_ = false;

    QTimer *pollTimer_ = nullptr;
    double lastPos_ = -1;
};
