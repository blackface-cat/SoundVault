#pragma once

#include <QString>
#include <QVector>

/**
 * 波形峰值分析器：用 miniaudio 解码音频，生成两级多分辨率峰值（粗 256 / 细 4096），
 * 供波形秒开显示（对应评审"多分辨率波形缓存：大文件秒开"）。
 * 缓存格式：SVP1 二进制（magic + 版本 + 采样率/声道/位深/时长 + 粗/细峰值数组）。
 *   v2 起：细峰值按左右声道分开存（fineL/fineR），供立体声双轨渲染。
 */
class WaveformAnalyzer
{
public:
    struct Peaks {
        QVector<float> coarse;   // 256 bins（多声道取最大，用于电平指示）
        QVector<float> fine;     // 4096 bins（单声道或混合）
        QVector<float> fineL;    // 4096 bins 左声道（立体声才有；单声道为空）
        QVector<float> fineR;    // 4096 bins 右声道（立体声才有；单声道为空）
        int sampleRate = 0;
        int channels = 0;
        int bitDepth = 0;
        double duration = 0;
    };

    /// 解码并计算峰值（支持 wav / mp3 / flac / ogg / aiff）
    static bool analyze(const QString &path, Peaks &out);

    // ---- 频谱图（Audition 风格 spectrogram） ----
    struct Spectrogram {
        int cols = 0;            // 时间列（横向）
        int rows = 0;            // 频率行（纵向，低频在底部）
        QVector<quint8> data;    // cols × rows，幅度 dB 量化 0..255
        double duration = 0;
        int sampleRate = 0;
        double fMin = 0, fMax = 0;   // 频率范围（Hz）
    };

    /// 一次解码同时产出峰值 + 频谱（v1.3：避免老版两遍解码，提速约一倍）
    struct Analysis {
        Peaks peaks;
        Spectrogram spectrogram;
        bool hasSpectrogram = false;
    };
    static bool analyzeFull(const QString &path, Analysis &out, bool wantSpectrogram = true);

    /// 解码 + STFT 生成频谱图（低分辨率、快）
    static bool analyzeSpectrogram(const QString &path, Spectrogram &out);

    /// 导出选区 [t0,t1] 秒为 16bit PCM WAV 文件（用于拖进宿主软件）
    static bool exportSegment(const QString &path, double t0, double t1,
                              const QString &outWavPath);

    /// 读取 WAV 文件头的位深（fmt 块）；非 WAV 或读取失败返回 0
    static int wavBitDepth(const QString &path);

    /// 保存 / 读取缓存文件
    static bool saveCache(const QString &cachePath, const Peaks &peaks);
    static bool loadCache(const QString &cachePath, Peaks *out);

    /// 从缓存文件读取 Peaks（失败返回空结构）
    static Peaks peaksFromCache(const QString &cachePath);

    static bool saveSpectrogram(const QString &cachePath, const Spectrogram &s);
    static bool loadSpectrogram(const QString &cachePath, Spectrogram *out);
};
