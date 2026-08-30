// 播放引擎真实链路测试：枚举设备 → 载入 WAV → 播放 → 验证进度推进 → 结束
#include <QCoreApplication>
#include <QTimer>
#include <QDebug>
#include <QFileInfo>
#include <QDir>

#include "audio/AudioEngine.h"
#include "audio/WaveformAnalyzer.h"
#include "db/MetadataStore.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    AudioEngine engine;

    // 1) 设备枚举
    const auto devs = engine.outputDevices();
    qDebug() << "[1] 播放设备数:" << devs.size();
    for (const auto &d : devs)
        qDebug() << "    -" << d.name << (d.isDefault ? "(默认)" : "");
    if (!engine.isUsable()) {
        qWarning() << "引擎不可用:" << engine.error();
        return 1;
    }
    qDebug() << "[1] 当前输出:" << engine.currentDeviceName();

    // 2) 找一个测试 WAV（系统自带）
    QString wav = QStringLiteral("C:/Windows/Media/Windows Background.wav");
    if (!QFileInfo::exists(wav)) {
        const auto cands = QDir(QStringLiteral("C:/Windows/Media"))
                               .entryInfoList({QStringLiteral("*.wav")}, QDir::Files);
        if (!cands.isEmpty())
            wav = cands.first().absoluteFilePath();
    }
    qDebug() << "[2] 测试文件:" << wav;

    // 3) 波形分析
    WaveformAnalyzer::Peaks pk;
    const bool analyzed = WaveformAnalyzer::analyze(wav, pk);
    qDebug() << "[3] 波形分析:" << (analyzed ? "OK" : "FAIL")
             << "dur=" << pk.duration << "sr=" << pk.sampleRate
             << "ch=" << pk.channels << "bits=" << pk.bitDepth;

    // 4) 频谱分析
    WaveformAnalyzer::Spectrogram sp;
    const bool specOk = WaveformAnalyzer::analyzeSpectrogram(wav, sp);
    int nonzero = 0;
    for (quint8 v : sp.data)
        if (v > 0)
            ++nonzero;
    qDebug() << "[4] 频谱分析:" << (specOk ? "OK" : "FAIL")
             << "cols=" << sp.cols << "rows=" << sp.rows
             << "dataSize=" << sp.data.size()
             << "非零像素=" << nonzero
             << QStringLiteral("(%1%)").arg(sp.data.isEmpty() ? 0 : nonzero * 100 / sp.data.size());

    // 4b) 缓存存读回环
    const QString tmpSpec = QDir::temp().absoluteFilePath(QStringLiteral("svp2_test.svp2"));
    WaveformAnalyzer::saveSpectrogram(tmpSpec, sp);
    WaveformAnalyzer::Spectrogram sp2;
    const bool loadOk = WaveformAnalyzer::loadSpectrogram(tmpSpec, &sp2);
    qDebug() << "[4b] 频谱缓存存读:" << (loadOk ? "OK" : "FAIL")
             << "cols=" << sp2.cols << "rows=" << sp2.rows << "size=" << sp2.data.size();

    // 4c) 选区导出（框选段落 → 16bit WAV）
    const QString tmpWav = QDir::temp().absoluteFilePath(QStringLiteral("svp_seg_test.wav"));
    const bool segOk = WaveformAnalyzer::exportSegment(wav, 0.1, 0.5, tmpWav);
    qDebug() << "[4c] 选区导出:" << (segOk ? "OK" : "FAIL")
             << "size=" << (segOk ? QFileInfo(tmpWav).size() : 0)
             << "head=" << (segOk ? WaveformAnalyzer::wavBitDepth(tmpWav) : 0) << "bit";

    // 5) 载入 + 播放，每 200ms 轮询一次
    if (!engine.loadFile(wav)) {
        qWarning() << "载入失败";
        return 1;
    }
    qDebug() << "[4] 载入 OK, duration=" << engine.duration();
    engine.play();

    auto *poll = new QTimer(&app);
    poll->setInterval(200);
    int ticks = 0;
    QObject::connect(poll, &QTimer::timeout, [&] {
        ++ticks;
        qDebug() << "   t=" << ticks * 0.2 << "s pos=" << engine.position()
                 << "state=" << int(engine.state());
        if (ticks >= 10) {
            poll->stop();
            bool progressed = false;
            engine.play();
            QTimer::singleShot(500, [&] {
                progressed = engine.position() > 0.1;
                engine.stop();
                if (progressed) {
                    qDebug() << "PLAYTEST PASS";
                } else {
                    qWarning() << "PLAYTEST FAIL（进度未推进）";
                }
                app.quit();
            });
        }
    });
    poll->start();

    return app.exec();
}
