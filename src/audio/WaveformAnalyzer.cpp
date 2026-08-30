#include "audio/WaveformAnalyzer.h"

#include <QFile>
#include <QDataStream>
#include <QtMath>
#include <complex>
#include <algorithm>
#include <vector>

#include <miniaudio/miniaudio.h>

namespace {
constexpr quint32 kMagic = 0x31505653;   // "SVP1"
constexpr quint32 kSpecMagic = 0x32505653; // "SVP2"
constexpr int kCoarseBins = 256;
constexpr int kFineBins = 4096;

// 频谱图参数：1024 点 FFT、256 频行（0~采样率/4）、最多 1024 时间列
constexpr int kFft = 1024;
constexpr int kRows = 256;
constexpr int kMaxCols = 1024;

// 迭代 radix-2 FFT（就地，inplace），n 必须为 2 的幂
void fft(std::complex<double> *a, int n, bool inverse)
{
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double ang = 2.0 * M_PI / len * (inverse ? -1 : 1);
        const std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<double> w(1, 0);
            for (int j = 0; j < len / 2; ++j) {
                const std::complex<double> u = a[i + j];
                const std::complex<double> v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
    if (inverse)
        for (int i = 0; i < n; ++i)
            a[i] /= n;
}

// 写 16bit PCM WAV 头（44 字节标准 RIFF/fmt/data 布局）
bool writeWavHeader(QFile &f, int sampleRate, int channels, quint32 dataBytes)
{
    const quint32 byteRate = quint32(sampleRate) * channels * 2;
    const quint16 blockAlign = quint16(channels * 2);
    const quint32 riffSize = 36 + dataBytes;
    QByteArray h;
    h.reserve(44);
    h += "RIFF";
    h.append(char(riffSize & 0xff)).append(char((riffSize >> 8) & 0xff))
     .append(char((riffSize >> 16) & 0xff)).append(char((riffSize >> 24) & 0xff));
    h += "WAVE";
    h += "fmt ";
    h.append(char(16)).append(char(0)).append(char(0)).append(char(0)); // fmt size
    h.append(char(1)).append(char(0));                                 // PCM
    h.append(char(channels & 0xff)).append(char((channels >> 8) & 0xff));
    h.append(char(sampleRate & 0xff)).append(char((sampleRate >> 8) & 0xff))
     .append(char((sampleRate >> 16) & 0xff)).append(char((sampleRate >> 24) & 0xff));
    h.append(char(byteRate & 0xff)).append(char((byteRate >> 8) & 0xff))
     .append(char((byteRate >> 16) & 0xff)).append(char((byteRate >> 24) & 0xff));
    h.append(char(blockAlign & 0xff)).append(char((blockAlign >> 8) & 0xff));
    h.append(char(16)).append(char(0));                                 // bits per sample
    h += "data";
    h.append(char(dataBytes & 0xff)).append(char((dataBytes >> 8) & 0xff))
     .append(char((dataBytes >> 16) & 0xff)).append(char((dataBytes >> 24) & 0xff));
    return f.write(h) == h.size();
}
} // namespace

int WaveformAnalyzer::wavBitDepth(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return 0;
    const QByteArray head = f.read(12);
    if (head.size() < 12 || !head.startsWith("RIFF") || !head.mid(8, 4).startsWith("WAVE"))
        return 0;
    while (!f.atEnd()) {
        const QByteArray chunkHead = f.read(8);
        if (chunkHead.size() < 8)
            break;
        const QByteArray chunkId = chunkHead.left(4);
        const quint32 size = quint32(quint8(chunkHead[4]))
                           | (quint32(quint8(chunkHead[5])) << 8)
                           | (quint32(quint8(chunkHead[6])) << 16)
                           | (quint32(quint8(chunkHead[7])) << 24);
        if (chunkId == "fmt ") {
            const QByteArray fmt = f.read(qMin<quint32>(size, 16));
            if (fmt.size() >= 16) {
                const quint16 bits = quint16(quint8(fmt[14]))
                                   | (quint16(quint8(fmt[15])) << 8);
                return bits == 0xfffe ? 32 : int(bits);
            }
            return 0;
        }
        f.seek(f.pos() + size + (size & 1));
    }
    return 0;
}

// ---------------------------------------------------------------- 单遍解码：峰值 + 频谱

bool WaveformAnalyzer::analyzeFull(const QString &path, Analysis &out, bool wantSpectrogram)
{
    ma_decoder decoder;
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
#ifdef Q_OS_WIN
    const std::wstring wpath = path.toStdWString();
    const ma_result initRes = ma_decoder_init_file_w(wpath.c_str(), &cfg, &decoder);
#else
    const ma_result initRes = ma_decoder_init_file(path.toUtf8().constData(), &cfg, &decoder);
#endif
    if (initRes != MA_SUCCESS)
        return false;

    const ma_uint32 channels = decoder.outputChannels;
    const ma_uint32 sampleRate = decoder.outputSampleRate;
    ma_uint64 totalFrames = 0;
    ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames);
    if (totalFrames == 0 || sampleRate == 0) {
        ma_decoder_uninit(&decoder);
        return false;
    }

    Peaks &p = out.peaks;
    p.sampleRate = int(sampleRate);
    p.channels = int(channels);
    p.bitDepth = wavBitDepth(path);
    p.duration = double(totalFrames) / double(sampleRate);
    p.coarse = QVector<float>(kCoarseBins, 0.f);
    p.fine = QVector<float>(kFineBins, 0.f);
    const bool stereo = channels >= 2;
    if (stereo) {
        p.fineL = QVector<float>(kFineBins, 0.f);
        p.fineR = QVector<float>(kFineBins, 0.f);
    }

    // 频谱状态（与峰值同一遍解码里完成）
    Spectrogram *sp = wantSpectrogram ? &out.spectrogram : nullptr;
    std::vector<float> window, win, hist;
    std::vector<std::complex<double>> spec;
    ma_uint64 hop = 0, nextEmit = 0, head = 0;
    int colIdx = 0;
    if (sp) {
        sp->rows = kRows;
        sp->cols = int(qMin<ma_uint64>(kMaxCols, qMax<ma_uint64>(1, totalFrames / 256)));
        hop = qMax<ma_uint64>(1, totalFrames / ma_uint64(sp->cols));
        sp->data.resize(sp->cols * sp->rows);
        sp->duration = p.duration;
        sp->sampleRate = int(sampleRate);
        sp->fMin = 0;
        sp->fMax = sampleRate / 2.0 * double(kRows) / double(kFft);
        window.resize(kFft);
        for (int i = 0; i < kFft; ++i)
            window[i] = float(0.5 * (1.0 - std::cos(2.0 * M_PI * i / (kFft - 1))));
        win.resize(kFft);
        hist.resize(kFft, 0.0f);
        spec.resize(kFft);
        nextEmit = kFft;
    }

    constexpr ma_uint32 kChunk = 8192;
    QVector<float> buf(kChunk * channels);
    ma_uint64 frameIndex = 0;

    for (;;) {
        ma_uint64 framesRead = 0;
        const ma_result res = ma_decoder_read_pcm_frames(&decoder, buf.data(), kChunk, &framesRead);
        if (framesRead == 0)
            break;

        for (ma_uint64 i = 0; i < framesRead; ++i) {
            const ma_uint64 idx = frameIndex + i;
            // 混合峰值（多声道取最大）
            float maxV = 0.f;
            for (ma_uint32 c = 0; c < channels; ++c)
                maxV = qMax(maxV, qAbs(buf[int(i * channels + c)]));
            const int fineBin = int(idx * kFineBins / totalFrames);
            if (fineBin < kFineBins && maxV > p.fine[fineBin])
                p.fine[fineBin] = maxV;
            const int coarseBin = int(idx * kCoarseBins / totalFrames);
            if (coarseBin < kCoarseBins && maxV > p.coarse[coarseBin])
                p.coarse[coarseBin] = maxV;
            // 左右声道独立峰值
            if (stereo) {
                const float l = qAbs(buf[int(i * channels + 0)]);
                const float r = qAbs(buf[int(i * channels + 1)]);
                if (fineBin < kFineBins) {
                    if (l > p.fineL[fineBin]) p.fineL[fineBin] = l;
                    if (r > p.fineR[fineBin]) p.fineR[fineBin] = r;
                }
            }
            // 频谱：环形缓冲最近 kFft 个 mono 采样，每隔 hop 帧出一次 FFT
            if (sp) {
                const float mono = (channels == 1)
                    ? buf[int(i * channels)]
                    : 0.5f * (buf[int(i * channels)] + buf[int(i * channels + 1)]);
                hist[head % kFft] = mono;
                ++head;
                if (head == nextEmit) {
                    for (int k = 0; k < kFft; ++k)
                        win[k] = hist[(head - kFft + ma_uint64(k)) % kFft];
                    for (int k = 0; k < kFft; ++k)
                        spec[k] = std::complex<double>(win[k] * window[k], 0.0);
                    fft(spec.data(), kFft, false);
                    for (int r = 0; r < kRows; ++r) {
                        const double mag = std::abs(spec[r]);
                        const double db = 20.0 * std::log10(mag + 1e-9);
                        const double norm = (db + 96.0) / 96.0;
                        const int v = int(qBound(0.0, norm * 255.0, 255.0));
                        sp->data[colIdx * kRows + (kRows - 1 - r)] = quint8(v);
                    }
                    ++colIdx;
                    if (colIdx >= sp->cols)
                        break;
                    nextEmit += hop;
                }
            }
        }
        frameIndex += framesRead;
        if (sp && colIdx >= sp->cols)
            break;
        if (res != MA_SUCCESS)
            break;
    }

    ma_decoder_uninit(&decoder);
    if (sp) {
        sp->cols = colIdx;
        sp->data.resize(sp->cols * sp->rows);
        out.hasSpectrogram = colIdx > 0;
    }
    return p.fine.size() > 0;
}

bool WaveformAnalyzer::analyze(const QString &path, Peaks &out)
{
    Analysis a;
    if (!analyzeFull(path, a, false))
        return false;
    out = a.peaks;
    return out.fine.size() > 0;
}

bool WaveformAnalyzer::analyzeSpectrogram(const QString &path, Spectrogram &out)
{
    Analysis a;
    if (!analyzeFull(path, a, true))
        return false;
    out = a.spectrogram;
    return a.hasSpectrogram;
}

// ---------------------------------------------------------------- 选区导出（16bit PCM WAV）

bool WaveformAnalyzer::exportSegment(const QString &path, double t0, double t1,
                                     const QString &outWavPath)
{
    if (t1 <= t0)
        return false;
    ma_decoder decoder;
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
#ifdef Q_OS_WIN
    const std::wstring wpath = path.toStdWString();
    if (ma_decoder_init_file_w(wpath.c_str(), &cfg, &decoder) != MA_SUCCESS)
        return false;
#else
    if (ma_decoder_init_file(path.toUtf8().constData(), &cfg, &decoder) != MA_SUCCESS)
        return false;
#endif
    const ma_uint32 channels = decoder.outputChannels;
    const ma_uint32 sampleRate = decoder.outputSampleRate;
    ma_uint64 totalFrames = 0;
    ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames);
    const ma_uint64 startFrame = ma_uint64(qMax(0.0, t0 * sampleRate));
    ma_uint64 endFrame = ma_uint64(qMin(double(totalFrames), t1 * sampleRate));
    if (endFrame <= startFrame || startFrame >= totalFrames) {
        ma_decoder_uninit(&decoder);
        return false;
    }
    const ma_uint64 count = endFrame - startFrame;
    if (ma_decoder_seek_to_pcm_frame(&decoder, startFrame) != MA_SUCCESS) {
        ma_decoder_uninit(&decoder);
        return false;
    }

    QFile f(outWavPath);
    if (!f.open(QIODevice::WriteOnly)) {
        ma_decoder_uninit(&decoder);
        return false;
    }
    // 先写占位头，最后回填 data 大小
    if (!writeWavHeader(f, int(sampleRate), int(channels), 0)) {
        f.close();
        ma_decoder_uninit(&decoder);
        return false;
    }

    constexpr ma_uint32 kChunk = 8192;
    QVector<float> buf(kChunk * channels);
    ma_uint64 remaining = count;
    ma_uint64 dataBytes = 0;
    while (remaining > 0) {
        const ma_uint32 want = ma_uint32(qMin<ma_uint64>(kChunk, remaining));
        ma_uint64 got = 0;
        if (ma_decoder_read_pcm_frames(&decoder, buf.data(), want, &got) != MA_SUCCESS || got == 0)
            break;
        QByteArray pcm;
        pcm.reserve(int(got * channels * 2));
        for (ma_uint64 i = 0; i < got; ++i) {
            for (ma_uint32 c = 0; c < channels; ++c) {
                const float v = buf[int(i * channels + c)];
                const qint16 s = qint16(qBound(-1.0f, v, 1.0f) * 32767.0f);
                pcm.append(char(s & 0xff)).append(char((s >> 8) & 0xff));
            }
        }
        f.write(pcm);
        dataBytes += ma_uint64(pcm.size());
        remaining -= got;
    }

    // 回填 RIFF 总长与 data 长度
    f.seek(4);
    const quint32 riffSize = quint32(36 + dataBytes);
    f.write(QByteArray(1, char(riffSize & 0xff)));
    f.write(QByteArray(1, char((riffSize >> 8) & 0xff)));
    f.write(QByteArray(1, char((riffSize >> 16) & 0xff)));
    f.write(QByteArray(1, char((riffSize >> 24) & 0xff)));
    f.seek(40);
    f.write(QByteArray(1, char(dataBytes & 0xff)));
    f.write(QByteArray(1, char((dataBytes >> 8) & 0xff)));
    f.write(QByteArray(1, char((dataBytes >> 16) & 0xff)));
    f.write(QByteArray(1, char((dataBytes >> 24) & 0xff)));
    f.close();
    ma_decoder_uninit(&decoder);
    return dataBytes > 0;
}

// ---------------------------------------------------------------- 缓存

bool WaveformAnalyzer::saveCache(const QString &cachePath, const Peaks &peaks)
{
    QFile f(cachePath);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    QDataStream ds(&f);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << quint32(kMagic) << quint32(2)
       << quint32(peaks.sampleRate) << quint32(peaks.channels) << quint32(peaks.bitDepth)
       << double(peaks.duration)
       << quint32(peaks.coarse.size()) << quint32(peaks.fine.size());
    for (float v : peaks.coarse)
        ds << v;
    for (float v : peaks.fine)
        ds << v;
    ds << quint32(peaks.fineL.size()) << quint32(peaks.fineR.size());
    for (float v : peaks.fineL)
        ds << v;
    for (float v : peaks.fineR)
        ds << v;
    return true;
}

bool WaveformAnalyzer::loadCache(const QString &cachePath, Peaks *out)
{
    QFile f(cachePath);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QDataStream ds(&f);
    ds.setByteOrder(QDataStream::LittleEndian);
    quint32 magic = 0, ver = 0, sr = 0, ch = 0, bd = 0;
    double dur = 0;
    quint32 coarseN = 0, fineN = 0;
    ds >> magic >> ver >> sr >> ch >> bd >> dur >> coarseN >> fineN;
    if (magic != kMagic || (ver != 1 && ver != 2)
        || coarseN == 0 || fineN == 0 || coarseN > 65536 || fineN > 65536)
        return false;
    Peaks p;
    p.sampleRate = int(sr);
    p.channels = int(ch);
    p.bitDepth = int(bd);
    p.duration = dur;
    p.coarse.resize(int(coarseN));
    p.fine.resize(int(fineN));
    for (auto &v : p.coarse)
        ds >> v;
    for (auto &v : p.fine)
        ds >> v;
    if (ver >= 2) {
        quint32 ln = 0, rn = 0;
        ds >> ln >> rn;
        if (ln > 65536 || rn > 65536)
            return false;
        p.fineL.resize(int(ln));
        p.fineR.resize(int(rn));
        for (auto &v : p.fineL)
            ds >> v;
        for (auto &v : p.fineR)
            ds >> v;
    }
    if (ds.status() != QDataStream::Ok)
        return false;
    if (out)
        *out = p;
    return true;
}

WaveformAnalyzer::Peaks WaveformAnalyzer::peaksFromCache(const QString &cachePath)
{
    Peaks p;
    loadCache(cachePath, &p);
    return p;
}

bool WaveformAnalyzer::saveSpectrogram(const QString &cachePath, const Spectrogram &s)
{
    QFile f(cachePath);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    QDataStream ds(&f);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << quint32(kSpecMagic) << quint32(1)
       << quint32(s.cols) << quint32(s.rows)
       << double(s.duration) << quint32(s.sampleRate)
       << double(s.fMin) << double(s.fMax);
    const QByteArray raw(reinterpret_cast<const char *>(s.data.constData()), s.data.size());
    ds << raw;
    return true;
}

bool WaveformAnalyzer::loadSpectrogram(const QString &cachePath, Spectrogram *out)
{
    QFile f(cachePath);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QDataStream ds(&f);
    ds.setByteOrder(QDataStream::LittleEndian);
    quint32 magic = 0, ver = 0, cols = 0, rows = 0, sr = 0;
    double dur = 0, fmin = 0, fmax = 0;
    ds >> magic >> ver >> cols >> rows >> dur >> sr >> fmin >> fmax;
    if (magic != kSpecMagic || ver != 1 || cols == 0 || rows == 0 || cols > 10000 || rows > 4096)
        return false;
    QByteArray raw;
    ds >> raw;
    if (raw.size() != int(cols) * int(rows))
        return false;
    Spectrogram s;
    s.cols = int(cols);
    s.rows = int(rows);
    s.duration = dur;
    s.sampleRate = int(sr);
    s.fMin = fmin;
    s.fMax = fmax;
    s.data.resize(raw.size());
    std::copy(raw.constBegin(), raw.constEnd(), s.data.begin());
    if (out)
        *out = s;
    return true;
}
