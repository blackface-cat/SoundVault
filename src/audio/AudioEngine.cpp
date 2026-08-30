#include "audio/AudioEngine.h"

#include <QDebug>
#include <QtMath>

#include <cstring>

namespace {
QByteArray deviceIdToBytes(const ma_device_id &id)
{
    return QByteArray(reinterpret_cast<const char *>(&id), sizeof(ma_device_id));
}

QString resultToString(ma_result r)
{
    return QString::fromLatin1(ma_result_description(r));
}
} // namespace

AudioEngine::AudioEngine(QObject *parent)
    : QObject(parent)
{
    // 默认后端优先级（Windows: WASAPI → DirectSound → WinMM），全部失败才报错
    if (ma_context_init(nullptr, 0, nullptr, &context_) == MA_SUCCESS) {
        contextReady_ = true;
    } else {
        error_ = QStringLiteral("音频系统初始化失败（WASAPI/DirectSound/WinMM 均不可用）");
        qWarning() << error_;
    }

    if (!rebuildEngine())
        qWarning() << "音频引擎初始化失败:" << error_;

    pollTimer_ = new QTimer(this);
    pollTimer_->setInterval(60);
    connect(pollTimer_, &QTimer::timeout, this, [this] {
        if (!hasSound_)
            return;
        const double pos = position();
        if (state_ == State::Playing && qAbs(pos - lastPos_) > 0.015) {
            lastPos_ = pos;
            emit positionChanged(pos);
        }
        // 自然播完
        if (state_ == State::Playing && ma_sound_at_end(&sound_)) {
            if (loop_) {
                ma_sound_seek_to_pcm_frame(&sound_, 0);
                ma_sound_start(&sound_);
            } else if (!endEmitted_) {
                endEmitted_ = true;
                state_ = State::Idle;
                pollTimer_->stop();
                emit positionChanged(duration());
                emit stateChanged();
                emit finished();
            }
        }
    });
}

AudioEngine::~AudioEngine()
{
    teardownSound();
    teardownEngine();
    if (contextReady_)
        ma_context_uninit(&context_);
}

// ---------------------------------------------------------------- 设备

QVector<AudioEngine::DeviceInfo> AudioEngine::outputDevices()
{
    QVector<DeviceInfo> out;
    if (!contextReady_)
        return out;
    ma_device_info *playbackInfos = nullptr;
    ma_uint32 playbackCount = 0;
    if (ma_context_get_devices(&context_, &playbackInfos, &playbackCount,
                               nullptr, nullptr) != MA_SUCCESS) {
        return out;
    }
    for (ma_uint32 i = 0; i < playbackCount; ++i) {
        DeviceInfo d;
        d.id = deviceIdToBytes(playbackInfos[i].id);
        d.name = QString::fromUtf8(playbackInfos[i].name);
        d.isDefault = (playbackInfos[i].isDefault != 0u);
        out.append(d);
    }
    return out;
}

void AudioEngine::teardownSound()
{
    if (hasSound_) {
        ma_sound_stop(&sound_);
        ma_sound_uninit(&sound_);
        hasSound_ = false;
    }
    state_ = State::Idle;
}

void AudioEngine::teardownEngine()
{
    if (engineReady_) {
        ma_engine_uninit(&engine_);
        engineReady_ = false;
    }
}

bool AudioEngine::rebuildEngine()
{
    const bool wasPlaying = (state_ == State::Playing);
    const QString playingPath = path_;
    teardownSound();
    teardownEngine();
    if (!contextReady_) {
        error_ = QStringLiteral("音频系统初始化失败（WASAPI/DirectSound/WinMM 均不可用）");
        return false;
    }

    ma_device_id chosenId{};
    bool hasChosen = false;
    if (!currentDeviceId_.isEmpty()) {
        const QVector<DeviceInfo> devs = outputDevices();
        for (const DeviceInfo &d : devs) {
            if (d.id == currentDeviceId_) {
                std::memcpy(&chosenId, d.id.constData(), sizeof(ma_device_id));
                hasChosen = true;
                break;
            }
        }
        if (!hasChosen)
            qWarning() << "已保存的输出设备不存在，回退系统默认";
    }

    const auto tryInit = [&](ma_device_id *deviceId, ma_result *outResult) -> bool {
        ma_engine_config cfg = ma_engine_config_init();
        cfg.pContext = &context_;
        if (deviceId)
            cfg.pPlaybackDeviceID = deviceId;
        const ma_result r = ma_engine_init(&cfg, &engine_);
        if (outResult)
            *outResult = r;
        return r == MA_SUCCESS;
    };

    if (hasChosen && tryInit(&chosenId, nullptr)) {
        currentDeviceName_ = QStringLiteral("(已选设备)");
        engineReady_ = true;
    } else if (tryInit(nullptr, nullptr)) {
        engineReady_ = true;
        currentDeviceName_ = QStringLiteral("系统默认");
        if (hasChosen) {
            error_ = QStringLiteral("所选输出设备不可用，已回退系统默认设备");
            emit deviceError(error_);
        }
    } else {
        ma_result r = MA_ERROR;
        tryInit(nullptr, &r);
        error_ = QStringLiteral("音频输出设备初始化失败：%1").arg(resultToString(r));
        currentDeviceName_.clear();
        emit deviceError(error_);
        return false;
    }

    // 确保引擎/设备处于运行状态（默认自动启动，这里兜底）
    if (ma_engine_start(&engine_) != MA_SUCCESS)
        qWarning() << "ma_engine_start 失败（可能已在运行）";

    // 用真实设备名刷新
    ma_device *dev = ma_engine_get_device(&engine_);
    if (dev) {
        ma_device_info info;
        if (ma_device_get_info(dev, ma_device_type_playback, &info) == MA_SUCCESS)
            currentDeviceName_ = QString::fromUtf8(info.name);
    }

    // 引擎重建后重载当前文件（之前在播则续播）
    if (!playingPath.isEmpty()) {
        path_.clear();
        if (loadFile(playingPath) && wasPlaying)
            play();
    }
    emit deviceChanged();
    return true;
}

bool AudioEngine::selectDevice(const QByteArray &deviceId)
{
    currentDeviceId_ = deviceId;
    return rebuildEngine();
}

QString AudioEngine::currentDeviceName() const
{
    if (currentDeviceName_.isEmpty())
        return QStringLiteral("无可用设备");
    return currentDeviceName_;
}

// ---------------------------------------------------------------- 播放

void AudioEngine::applyPlaybackParams()
{
    if (!hasSound_)
        return;
    ma_sound_set_volume(&sound_, float(qBound(0.0, volume_, 1.0)));
    ma_sound_set_pitch(&sound_, float(qBound(0.5, speed_, 2.0)));
    ma_sound_set_looping(&sound_, loop_ ? MA_TRUE : MA_FALSE);
}

bool AudioEngine::loadFile(const QString &path)
{
    if (!engineReady_) {
        emit loadFailed(path, error_.isEmpty() ? QStringLiteral("音频引擎未初始化") : error_);
        return false;
    }
    teardownSound();
    path_.clear();

    ma_result r = MA_ERROR;
#ifdef Q_OS_WIN
    const std::wstring wpath = path.toStdWString();
    r = ma_sound_init_from_file_w(&engine_, wpath.c_str(), MA_SOUND_FLAG_STREAM,
                                  nullptr, nullptr, &sound_);
#else
    r = ma_sound_init_from_file(&engine_, path.toUtf8().constData(), MA_SOUND_FLAG_STREAM,
                                nullptr, nullptr, &sound_);
#endif
    if (r != MA_SUCCESS) {
        const QString reason = QStringLiteral("无法解码（%1）").arg(resultToString(r));
        qWarning() << "载入失败:" << path << reason;
        emit loadFailed(path, reason);
        return false;
    }
    hasSound_ = true;
    path_ = path;
    lastPos_ = -1;
    endEmitted_ = false;
    applyPlaybackParams();
    emit stateChanged();
    return true;
}

void AudioEngine::play()
{
    if (!hasSound_ || state_ == State::Playing)
        return;
    if (state_ == State::Idle && ma_sound_at_end(&sound_))
        ma_sound_seek_to_pcm_frame(&sound_, 0);
    endEmitted_ = false;
    const ma_result startRes = ma_sound_start(&sound_);
    if (startRes != MA_SUCCESS) {
        qWarning() << "ma_sound_start 失败:" << startRes << resultToString(startRes);
        emit deviceError(QStringLiteral("播放启动失败（%1），请检查输出设备")
                             .arg(resultToString(startRes)));
        return;
    }
    state_ = State::Playing;
    pollTimer_->start();
    emit stateChanged();
}

void AudioEngine::pause()
{
    if (!hasSound_ || state_ != State::Playing)
        return;
    ma_sound_stop(&sound_);
    state_ = State::Paused;
    pollTimer_->stop();
    emit stateChanged();
}

void AudioEngine::stop()
{
    if (!hasSound_)
        return;
    ma_sound_stop(&sound_);
    ma_sound_seek_to_pcm_frame(&sound_, 0);
    state_ = State::Idle;
    pollTimer_->stop();
    lastPos_ = -1;
    emit stateChanged();
    emit positionChanged(0);
}

void AudioEngine::seekByRatio(double ratio)
{
    if (!hasSound_)
        return;
    ma_uint64 len = 0;
    ma_sound_get_length_in_pcm_frames(&sound_, &len);
    if (len == 0)
        return;
    ma_sound_seek_to_pcm_frame(&sound_,
        ma_uint64(qBound(0.0, ratio, 1.0) * double(len)));
    endEmitted_ = false;
    lastPos_ = -1;
    emit positionChanged(position());
}

void AudioEngine::seekBySecond(double sec)
{
    if (!hasSound_)
        return;
    const ma_uint32 sr = ma_engine_get_sample_rate(&engine_);
    if (sr == 0)
        return;
    ma_sound_seek_to_pcm_frame(&sound_, ma_uint64(qMax(0.0, sec) * double(sr)));
    endEmitted_ = false;
    lastPos_ = -1;
    emit positionChanged(position());
}

void AudioEngine::setVolume(double v)
{
    volume_ = qBound(0.0, v, 1.0);
    if (hasSound_)
        ma_sound_set_volume(&sound_, float(volume_));
}

void AudioEngine::setSpeed(double speed)
{
    speed_ = qBound(0.5, speed, 2.0);
    if (hasSound_)
        ma_sound_set_pitch(&sound_, float(speed_));
}

void AudioEngine::setLoop(bool on)
{
    loop_ = on;
    if (hasSound_)
        ma_sound_set_looping(&sound_, loop_ ? MA_TRUE : MA_FALSE);
}

double AudioEngine::duration() const
{
    if (!hasSound_)
        return 0;
    ma_uint64 len = 0;
    ma_sound_get_length_in_pcm_frames(&sound_, &len);
    const ma_uint32 sr = ma_engine_get_sample_rate(&engine_);
    return sr ? double(len) / double(sr) : 0.0;
}

double AudioEngine::position() const
{
    if (!hasSound_)
        return 0;
    ma_uint64 cur = 0;
    ma_sound_get_cursor_in_pcm_frames(&sound_, &cur);
    const ma_uint32 sr = ma_engine_get_sample_rate(&engine_);
    return sr ? double(cur) / double(sr) : 0.0;
}
