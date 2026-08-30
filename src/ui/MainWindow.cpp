#include "ui/MainWindow.h"
#include "ui/Icons.h"
#include "ui/FileListPanel.h"
#include "ui/DetailPanel.h"
#include "ui/PlayerPanel.h"
#include "audio/AudioEngine.h"
#include "audio/WaveformAnalyzer.h"
#include "agent/AgentService.h"
#include "agent/AgentHttpServer.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QToolBar>
#include <QSplitter>
#include <QMenuBar>
#include <QMenu>
#include <QStatusBar>
#include <QMessageBox>
#include <QFileDialog>
#include <QPushButton>
#include <QCheckBox>
#include <QSettings>
#include <QApplication>
#include <QStandardPaths>
#include <QtConcurrent>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QComboBox>
#include <QStackedWidget>
#include <QTreeView>
#include <QFileSystemModel>
#include <QListWidget>
#include <QTreeWidget>
#include <QTimer>
#include <QShortcut>
#include <QPointer>
#include <QFile>
#include <QInputDialog>
#include <QRandomGenerator>
#include <QDialog>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QColorDialog>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QDateTime>

namespace {

const char *kAudioFilters[] = { "wav", "mp3", "flac", "ogg", "aiff", "aif", nullptr };

QStringList audioNameFilters()
{
    QStringList f;
    for (int i = 0; kAudioFilters[i]; ++i)
        f << QStringLiteral("*.%1").arg(QLatin1String(kAudioFilters[i]));
    return f;
}

QString fmtBytes(qint64 b)
{
    if (b < 1024)
        return QStringLiteral("%1 B").arg(b);
    if (b < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(b / 1024.0, 0, 'f', 1);
    if (b < 1024LL * 1024 * 1024)
        return QStringLiteral("%1 MB").arg(b / 1024.0 / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(b / 1024.0 / 1024.0 / 1024.0, 0, 'f', 2);
}

constexpr int kMaxFilesPerScan = 20000;

// 频谱图数据 → Audition 式热度色 QImage
QImage spectrogramToImage(const WaveformAnalyzer::Spectrogram &sp)
{
    QImage img(sp.cols, sp.rows, QImage::Format_RGB32);
    for (int y = 0; y < sp.rows; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < sp.cols; ++x) {
            const int v = sp.data[x * sp.rows + y];  // 0..255
            const double t = v / 255.0;
            // 黑 → 蓝 → 青 → 绿 → 黄 → 红 → 白
            int r = 0, g = 0, b = 0;
            if (t < 0.25) {
                const double k = t / 0.25;
                r = 0; g = 0; b = int(40 + 200 * k);
            } else if (t < 0.5) {
                const double k = (t - 0.25) / 0.25;
                r = 0; g = int(200 * k); b = 255;
            } else if (t < 0.75) {
                const double k = (t - 0.5) / 0.25;
                r = int(255 * k); g = 255; b = int(255 * (1 - k));
            } else {
                const double k = (t - 0.75) / 0.25;
                r = 255; g = int(255 * (1 - k * 0.4)); b = int(255 * (1 - k));
            }
            line[x] = qRgb(r, g, b);
        }
    }
    return img;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("声库 Sound Vault — 音频素材工作站 v%1").arg(Theme::appVersion()));
    resize(1440, 900);
    setMinimumSize(1080, 680);

    engine_ = new AudioEngine(this);

    QSettings s;
    QString cacheDir = s.value(QStringLiteral("cache/dir")).toString();
    if (cacheDir.isEmpty() || !QFileInfo::exists(cacheDir))
        cacheDir = defaultCacheDir();
    if (!store_.open(cacheDir))
        QMessageBox::warning(this, QStringLiteral("声库"),
            QStringLiteral("无法创建缓存文件夹：%1\n标签/收藏将无法保存。").arg(cacheDir));

    // 内置数据：多维 facet + UCS 分类树
    QFile facetsFile(QStringLiteral(":/data/facets.json"));
    if (facetsFile.open(QIODevice::ReadOnly))
        store_.ensureFacetCatalog(facetsFile.readAll());
    QFile ucsFile(QStringLiteral(":/data/ucs_tree.json"));
    if (ucsFile.open(QIODevice::ReadOnly))
        store_.ensureUcsCatalog(ucsFile.readAll());

    const int vol = s.value(QStringLiteral("audio/volume"), 70).toInt();
    const int spd = s.value(QStringLiteral("audio/speed"), 100).toInt();
    engine_->setVolume(vol / 100.0);
    engine_->setSpeed(spd / 100.0);

    agent_ = new AgentService(&store_, engine_, this);
    AgentService::AuthScope scope;
    scope.allowWrite = true;
    agent_->setAuthScope(scope);

    createTopBar();
    createCentral();
    createMenus();
    applyTheme(s.value(QStringLiteral("ui/dark"), false).toBool());
    playerPanel_->setVolumeUi(vol);
    playerPanel_->setSpeedUi(spd);

    // ---- 播放引擎接线 ----
    connect(playerPanel_, &PlayerPanel::playRequested, engine_, &AudioEngine::play);
    connect(playerPanel_, &PlayerPanel::pauseRequested, engine_, &AudioEngine::pause);
    connect(playerPanel_, &PlayerPanel::stopRequested, engine_, &AudioEngine::stop);
    connect(playerPanel_, &PlayerPanel::seekRequested, engine_, &AudioEngine::seekByRatio);
    connect(playerPanel_, &PlayerPanel::volumeChanged, this, [this](double v) {
        engine_->setVolume(v);
        QSettings().setValue(QStringLiteral("audio/volume"), int(v * 100));
    });
    connect(playerPanel_, &PlayerPanel::speedChanged, this, [this](double v) {
        engine_->setSpeed(v);
        QSettings().setValue(QStringLiteral("audio/speed"), int(v * 100));
    });
    connect(playerPanel_, &PlayerPanel::prevRequested, this, [this] { filePanel_->stepSelection(-1); });
    connect(playerPanel_, &PlayerPanel::nextRequested, this, [this] { filePanel_->stepSelection(+1); });
    connect(playerPanel_, &PlayerPanel::loopToggled, engine_, &AudioEngine::setLoop);
    connect(playerPanel_, &PlayerPanel::contToggled, this, [this](bool on) { contPlay_ = on; });

    // 选区导出回调（框选段落 → 拖出到宿主 / 导出文件）
    playerPanel_->setSegmentExporter([this](double t0, double t1) {
        return exportCurrentSegment(t0, t1);
    });

    connect(engine_, &AudioEngine::stateChanged, this, [this] {
        playerPanel_->setState(engine_->state() == AudioEngine::State::Playing,
                               !engine_->currentPath().isEmpty());
    });
    connect(engine_, &AudioEngine::positionChanged, this, [this](double pos) {
        const double dur = engine_->duration();
        playerPanel_->setDuration(dur);
        playerPanel_->setPlayPosition(dur > 0 ? pos / dur : 0);
    });
    connect(engine_, &AudioEngine::finished, this, [this] {
        if (filePanel_->records().isEmpty()) { engine_->stop(); return; }
        if (!contPlay_) { engine_->stop(); return; }
        if (!filePanel_->stepSelection(+1))
            engine_->stop();
    });
    connect(engine_, &AudioEngine::loadFailed, this, [this](const QString &path, const QString &reason) {
        statusBar()->showMessage(QStringLiteral("播放失败：%1（%2）").arg(path, reason), 6000);
    });
    connect(engine_, &AudioEngine::deviceError, this, [this](const QString &msg) {
        statusBar()->showMessage(msg, 8000);
        QMessageBox::warning(this, QStringLiteral("音频设备"), msg);
    });

    // ---- 列表 / 详情接线 ----
    connect(filePanel_, &FileListPanel::fileSelected, this, &MainWindow::onFileSelected);
    connect(filePanel_, &FileListPanel::togglePlayRequested, this, &MainWindow::onTogglePlay);
    connect(filePanel_, &FileListPanel::tagFilterSelected, this, [this](qint64 tagId) {
        tagFilter_ = tagId;
        refilter();
    });
    connect(filePanel_, &FileListPanel::favoriteToggled, this, [this](const QString &path, bool fav) {
        store_.setFavorite(path, fav);
        updateFileUserMeta(path);
    });
    connect(filePanel_, &FileListPanel::rootFolderRequested, this, &MainWindow::addLibraryFlow);
    connect(filePanel_, &FileListPanel::rootFolderDropped, this, [this](const QString &path) {
        if (QFileInfo(path).isDir())
            scanFolder(path);
    });
    connect(filePanel_, &FileListPanel::fileDropped, this, &MainWindow::playPath);
    connect(filePanel_, &FileListPanel::batchTagRequested, this, &MainWindow::openBatchTagDialog);

    connect(detailPanel_, &DetailPanel::favoriteToggled, this, [this](const QString &path, bool fav) {
        store_.setFavorite(path, fav);
        updateFileUserMeta(path);
    });
    connect(detailPanel_, &DetailPanel::ratingChanged, this, [this](const QString &path, int rating) {
        store_.setRating(path, rating);
        updateFileUserMeta(path);
    });
    connect(detailPanel_, &DetailPanel::notesChanged, this, [this](const QString &path, const QString &notes) {
        store_.setNotes(path, notes);
    });
    connect(detailPanel_, &DetailPanel::tagAssigned, this, [this](const QString &path, qint64 tagId) {
        store_.assignTag(path, tagId);
        updateFileUserMeta(path);
        refreshTagChips();
    });
    connect(detailPanel_, &DetailPanel::tagRemoved, this, [this](const QString &path, qint64 tagId) {
        store_.unassignTag(path, tagId);
        updateFileUserMeta(path);
        refreshTagChips();
    });
    connect(detailPanel_, &DetailPanel::tagCreateRequested, this,
            [this](const QString &path, const QString &name) {
        if (name.trimmed().isEmpty())
            return;
        const qint64 tagId = store_.addTag(name.trimmed());
        if (tagId > 0) {
            store_.assignTag(path, tagId);
            updateFileUserMeta(path);
            refreshTagChips();
        }
    });
    connect(detailPanel_, &DetailPanel::facetToggled, this,
            [this](const QString &path, const QString &dim, const QString &tagId, bool on) {
        store_.setFacetTag(path, dim, tagId, on);
        updateFileUserMeta(path);
        refreshFacetTree();
    });
    connect(detailPanel_, &DetailPanel::tagManagerRequested, this, &MainWindow::openTagManager);
    connect(detailPanel_, &DetailPanel::ucsChanged, this,
            [this](const QString &path, const QString &catId) {
        // 清除时：若文件名仍能匹配 UCS，则自动加回（"删除后探测到对应名字自动加回"）
        if (catId.isEmpty()) {
            const QString detected = store_.detectUcs(QFileInfo(path).fileName());
            if (!detected.isEmpty()) {
                store_.setUcs(path, detected, true);
                updateFileUserMeta(path);
                refreshUcsTree();
                statusBar()->showMessage(
                    QStringLiteral("UCS 自动识别已恢复：%1").arg(detected), 3000);
                return;
            }
        }
        store_.setUcs(path, catId);
        updateFileUserMeta(path);
        refreshUcsTree();
    });
    connect(detailPanel_, &DetailPanel::kindChanged, this,
            [this](const QString &path, const QString &kind) {
        store_.setKind(path, kind);
        updateFileUserMeta(path);
        refreshKindList();
    });

    // ---- 搜索 ----
    searchDebounce_ = new QTimer(this);
    searchDebounce_->setSingleShot(true);
    searchDebounce_->setInterval(250);
    connect(searchDebounce_, &QTimer::timeout, this, [this] {
        const QString t = searchField_->text().trimmed();
        if (t != searchText_) {
            searchText_ = t;
            if (!t.isEmpty())
                store_.recordSearch(t);
            refilter();
        }
    });
    connect(searchField_, &QLineEdit::textChanged, this, [this] { searchDebounce_->start(); });

    // ---- 目录树：点文件夹名字即可展开 + 打开（不用去点小箭头） ----
    folderTree_->setExpandsOnDoubleClick(false);
    connect(folderTree_, &QTreeView::clicked, this, [this](const QModelIndex &idx) {
        const QString path = fsModel_->filePath(idx);
        if (!QFileInfo(path).isDir())
            return;
        // 单击文件夹名：切换展开，并直接浏览该文件夹
        const bool expanded = folderTree_->isExpanded(idx);
        folderTree_->setExpanded(idx, !expanded);
        folderTree_->setCurrentIndex(idx);
        scanFolder(path);
    });
    connect(subdirsCheck_, &QCheckBox::toggled, this, [this](bool on) {
        includeSubdirs_ = on;
        QSettings().setValue(QStringLiteral("ui/includeSubdirs"), on);
        if (!currentFolder_.isEmpty())
            scanFolder(currentFolder_);
    });

    // ---- 元数据变化 → 刷新侧栏 ----
    connect(&store_, &MetadataStore::dataChanged, this, [this] {
        refreshTagChips();
        refreshFacetTree();
    });

    // F5 刷新
    auto *f5 = new QShortcut(QKeySequence(Qt::Key_F5), this);
    connect(f5, &QShortcut::activated, this, [this] {
        if (!currentFolder_.isEmpty())
            scanFolder(currentFolder_);
    });

    // ---- 启动 ----
    restoreDeviceSetting();
    refreshTagChips();
    refreshFacetTree();
    refreshUcsTree();
    refreshKindList();
    refreshFavGroups();
    refreshHistory();

    const int viewMode = qBound(0, s.value(QStringLiteral("ui/viewMode"), 0).toInt(), 1);
    if (viewMode == 1)
        cardViewBtn_->click();
    if (s.value(QStringLiteral("ui/detailVisible"), false).toBool()) {
        detailToggle_->setChecked(true);
        detailPanel_->setVisible(true);
    }

    // 默认浏览上一次的文件夹，否则回落到系统「音乐」目录（保证打开就有内容）
    QString lastDir = s.value(QStringLiteral("ui/lastFolder")).toString();
    if (lastDir.isEmpty() || !QFileInfo::exists(lastDir)) {
        lastDir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        if (lastDir.isEmpty() || !QFileInfo::exists(lastDir))
            lastDir = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    }
    if (!lastDir.isEmpty() && QFileInfo::exists(lastDir))
        scanFolder(lastDir);

    // Agent 可请求跳到某个文件夹
    connect(agent_, &AgentService::browseRequested, this, [this](const QString &path) {
        scanFolder(path);
    });

    statusDeviceLabel_->setText(QStringLiteral("输出：%1").arg(engine_->currentDeviceName()));
    statusDeviceLabel_->setToolTip(QStringLiteral("缓存文件夹：%1").arg(store_.cacheDir()));

    startAgentServer();
}

QString MainWindow::defaultCacheDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/cache");
}

QToolButton *MainWindow::makeIconButton(const char *svgBody, const QString &tip, bool checkable)
{
    auto *btn = new QToolButton(this);
    btn->setObjectName(QStringLiteral("IconBtn"));
    btn->setIconSize(QSize(16, 16));
    btn->setCheckable(checkable);
    btn->setToolTip(tip);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setProperty("iconBody", QString::fromLatin1(svgBody));
    return btn;
}

void MainWindow::startAgentServer()
{
    agentServer_ = new AgentHttpServer(agent_, this);
    const quint16 port = agentServer_->start();
    if (agentStatusLabel_) {
        if (port != 0) {
            agentStatusLabel_->setText(QStringLiteral("AI 接口 127.0.0.1:%1").arg(port));
            agentStatusLabel_->setToolTip(
                QStringLiteral("把《声库-AI接入卡.md》发给任意 AI Agent 即可接管软件。\n"
                               "接口：GET /status · GET /tools · POST /call"));
        } else {
            agentStatusLabel_->setText(QStringLiteral("AI 接口未启动（端口被占用）"));
        }
    }
}

// ---------------------------------------------------------------- 顶栏

void MainWindow::createTopBar()
{
    auto *bar = new QToolBar(this);
    bar->setObjectName(QStringLiteral("TopBar"));
    bar->setMovable(false);
    bar->setFloatable(false);
    bar->setIconSize(QSize(16, 16));
    addToolBar(Qt::TopToolBarArea, bar);

    auto *logo = new QLabel(bar);
    logo->setObjectName(QStringLiteral("AppLogo"));
    logo->setPixmap(QIcon(QStringLiteral(":/assets/logo.png")).pixmap(22, 22));
    bar->addWidget(logo);

    auto *title = new QLabel(QStringLiteral("声库"), bar);
    title->setObjectName(QStringLiteral("AppTitle"));
    bar->addWidget(title);
    bar->addSeparator();

    searchField_ = new QLineEdit(bar);
    searchField_->setObjectName(QStringLiteral("SearchField"));
    searchField_->setPlaceholderText(QStringLiteral("搜索素材（文件名/路径/标签/UCS）"));
    searchField_->setClearButtonEnabled(true);
    searchField_->setMinimumWidth(240);
    searchField_->setMaximumWidth(420);
    searchField_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bar->addWidget(searchField_);

    statsLabel_ = new QLabel(QStringLiteral("共 0 个素材"), bar);
    statsLabel_->setObjectName(QStringLiteral("StatsLabel"));
    statsLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    bar->addWidget(statsLabel_);
    bar->addSeparator();

    listViewBtn_ = makeIconButton(Icons::Path::List, QStringLiteral("列表视图"), true);
    listViewBtn_->setChecked(true);
    cardViewBtn_ = makeIconButton(Icons::Path::Grid, QStringLiteral("卡片视图"), true);
    connect(listViewBtn_, &QToolButton::clicked, this, [this] {
        filePanel_->setViewMode(0);
        listViewBtn_->setChecked(true); cardViewBtn_->setChecked(false);
        QSettings().setValue(QStringLiteral("ui/viewMode"), 0);
    });
    connect(cardViewBtn_, &QToolButton::clicked, this, [this] {
        filePanel_->setViewMode(1);
        listViewBtn_->setChecked(false); cardViewBtn_->setChecked(true);
        QSettings().setValue(QStringLiteral("ui/viewMode"), 1);
    });
    bar->addWidget(listViewBtn_);
    bar->addWidget(cardViewBtn_);
    bar->addSeparator();

    auto *devCap = new QLabel(QStringLiteral("输出"), bar);
    devCap->setObjectName(QStringLiteral("VolLabel"));
    bar->addWidget(devCap);
    deviceCombo_ = new QComboBox(bar);
    deviceCombo_->setMinimumWidth(160);
    deviceCombo_->setMaximumWidth(260);
    deviceCombo_->setToolTip(QStringLiteral("选择音频输出设备（直播机默认设备常是虚拟声卡，请选实际听歌设备）"));
    connect(deviceCombo_, QOverload<int>::of(&QComboBox::activated), this, [this](int idx) {
        const QByteArray id = deviceCombo_->itemData(idx).toByteArray();
        if (engine_->selectDevice(id)) {
            QSettings().setValue(QStringLiteral("audio/deviceId"), id.toHex());
            statusDeviceLabel_->setText(QStringLiteral("输出：%1").arg(engine_->currentDeviceName()));
        }
    });
    bar->addWidget(deviceCombo_);

    refreshBtn_ = makeIconButton(Icons::Path::Repeat, QStringLiteral("刷新（F5）"), false);
    connect(refreshBtn_, &QToolButton::clicked, this, [this] {
        repopulateDevices();
        if (!currentFolder_.isEmpty())
            scanFolder(currentFolder_);
    });
    bar->addWidget(refreshBtn_);
    bar->addSeparator();

    detailToggle_ = makeIconButton(Icons::Path::Info, QStringLiteral("显示/隐藏详情"), true);
    detailToggle_->setChecked(false);
    connect(detailToggle_, &QToolButton::clicked, this, [this](bool checked) {
        if (detailPanel_)
            detailPanel_->setVisible(checked);
        QSettings().setValue(QStringLiteral("ui/detailVisible"), checked);
    });
    bar->addWidget(detailToggle_);

    themeToggle_ = makeIconButton(Icons::Path::Sun, QStringLiteral("切换亮/暗主题"), true);
    connect(themeToggle_, &QToolButton::clicked, this, [this](bool checked) {
        dark_ = checked;
        applyTheme(dark_);
        QSettings().setValue(QStringLiteral("ui/dark"), dark_);
    });
    bar->addWidget(themeToggle_);
}

void MainWindow::repopulateDevices()
{
    if (!deviceCombo_)
        return;
    const QByteArray cur = engine_->currentDeviceId();
    deviceCombo_->blockSignals(true);
    deviceCombo_->clear();
    deviceCombo_->addItem(QStringLiteral("系统默认"), QByteArray());
    const auto devs = engine_->outputDevices();
    int selectIdx = 0;
    for (int i = 0; i < devs.size(); ++i) {
        const QString label = devs[i].isDefault
            ? QStringLiteral("%1（默认）").arg(devs[i].name) : devs[i].name;
        deviceCombo_->addItem(label, devs[i].id);
        if (!cur.isEmpty() && devs[i].id == cur)
            selectIdx = i + 1;
    }
    deviceCombo_->setCurrentIndex(selectIdx);
    deviceCombo_->blockSignals(false);
    statusDeviceLabel_->setText(QStringLiteral("输出：%1").arg(engine_->currentDeviceName()));
}

void MainWindow::restoreDeviceSetting()
{
    const QByteArray hex = QSettings().value(QStringLiteral("audio/deviceId")).toByteArray();
    if (!hex.isEmpty())
        engine_->selectDevice(QByteArray::fromHex(hex));
    repopulateDevices();
}

// ---------------------------------------------------------------- 布局

void MainWindow::buildNavRail(QWidget *host, QVBoxLayout *hostLayout)
{
    const struct { const char *icon; const char *tip; } items[] = {
        { Icons::Path::Folder,   "资源管理器" },
        { Icons::Path::Category, "UCS 分类" },
        { Icons::Path::Audio,    "业务大类" },
        { Icons::Path::Tag,      "分类 / 标签" },
        { Icons::Path::Star,     "收藏" },
        { Icons::Path::Clock,    "历史" },
    };
    const int n = int(sizeof(items) / sizeof(items[0]));
    for (int i = 0; i < n; ++i) {
        auto *btn = new QToolButton(host);
        btn->setObjectName(QStringLiteral("NavBtn"));
        btn->setCheckable(true);
        btn->setAutoExclusive(true);
        btn->setIconSize(QSize(20, 20));
        btn->setToolTip(QString::fromUtf8(items[i].tip));
        btn->setCursor(Qt::PointingHandCursor);
        btn->setProperty("iconBody", QString::fromLatin1(items[i].icon));
        connect(btn, &QToolButton::clicked, this, [this, i] { switchSide(i); });
        hostLayout->addWidget(btn);
        navButtons_.append(btn);
    }
    navButtons_.first()->setChecked(true);
}

void MainWindow::switchSide(int index)
{
    for (int i = 0; i < navButtons_.size(); ++i)
        navButtons_[i]->setChecked(i == index);
    sideStack_->setCurrentIndex(qBound(0, index, sideStack_->count() - 1));
    switch (index) {
    case 1: refreshUcsTree(); break;
    case 2: refreshKindList(); break;
    case 3: refreshFacetTree(); break;
    case 4: refreshFavGroups(); break;
    case 5: refreshHistory(); break;
    default: break;
    }
}

void MainWindow::buildSidePanels()
{
    // ---- 页 0：资源管理器（完整文件系统） ----
    auto *fsPage = new QWidget();
    auto *fsp = new QVBoxLayout(fsPage);
    fsp->setContentsMargins(8, 8, 8, 8);
    fsp->setSpacing(6);

    auto *fsHead = new QHBoxLayout();
    auto *fsTitle = new QLabel(QStringLiteral("资源管理器"), fsPage);
    fsTitle->setObjectName(QStringLiteral("SideSection"));
    fsHead->addWidget(fsTitle);
    fsHead->addStretch(1);
    auto *addLib = new QToolButton(fsPage);
    addLib->setObjectName(QStringLiteral("IconBtn"));
    addLib->setProperty("iconBody", QString::fromLatin1(Icons::Path::Star));
    addLib->setIcon(Icons::make(Icons::Path::Star, Theme::colors(false).muted));
    addLib->setToolTip(QStringLiteral("收藏当前文件夹"));
    connect(addLib, &QToolButton::clicked, this, [this] {
        if (!currentFolder_.isEmpty())
            store_.addLibrary(currentFolder_);
    });
    fsHead->addWidget(addLib);
    fsp->addLayout(fsHead);

    fsModel_ = new QFileSystemModel(this);
    fsModel_->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot);
    fsModel_->setRootPath(QString());
    folderTree_ = new QTreeView(fsPage);
    folderTree_->setModel(fsModel_);
    folderTree_->setHeaderHidden(true);
    folderTree_->setMinimumWidth(190);
    folderTree_->setRootIndex(fsModel_->index(QString()));
    for (int c = 1; c < fsModel_->columnCount(); ++c)
        folderTree_->setColumnHidden(c, true);
    // 只显示驱动器列
    fsp->addWidget(folderTree_, 1);

    subdirsCheck_ = new QCheckBox(QStringLiteral("包含子文件夹"), fsPage);
    includeSubdirs_ = QSettings().value(QStringLiteral("ui/includeSubdirs"), false).toBool();
    subdirsCheck_->setChecked(includeSubdirs_);
    fsp->addWidget(subdirsCheck_);
    sideStack_->addWidget(fsPage);

    // ---- 页 1：UCS 分类 ----
    auto *ucsPage = new QWidget();
    auto *up = new QVBoxLayout(ucsPage);
    up->setContentsMargins(8, 8, 8, 8);
    up->setSpacing(6);
    auto *ucsTitle = new QLabel(QStringLiteral("UCS 分类"), ucsPage);
    ucsTitle->setObjectName(QStringLiteral("SideSection"));
    up->addWidget(ucsTitle);
    ucsTree_ = new QTreeWidget(ucsPage);
    ucsTree_->setHeaderHidden(true);
    ucsTree_->setObjectName(QStringLiteral("SideTree"));
    ucsTree_->setExpandsOnDoubleClick(false);
    ucsTree_->setIndentation(18);
    up->addWidget(ucsTree_, 1);
    connect(ucsTree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int) {
        // 点名字即可展开/收起（像资源管理器，不必去找小箭头）
        if (item->childCount() > 0) {
            item->setExpanded(!item->isExpanded());
            return;
        }
        const QString catId = item->data(0, Qt::UserRole).toString();
        if (!catId.isEmpty())
            showGlobalFiles(store_.filesWithUcs(catId),
                            QStringLiteral("UCS · %1").arg(item->text(0)));
    });
    sideStack_->addWidget(ucsPage);

    // ---- 页 2：业务大类（环境/拟声/特殊） ----
    auto *kindPage = new QWidget();
    auto *kp = new QVBoxLayout(kindPage);
    kp->setContentsMargins(8, 8, 8, 8);
    kp->setSpacing(6);
    auto *kindTitle = new QLabel(QStringLiteral("业务大类"), kindPage);
    kindTitle->setObjectName(QStringLiteral("SideSection"));
    kp->addWidget(kindTitle);
    kindList_ = new QListWidget(kindPage);
    kindList_->setObjectName(QStringLiteral("SideTree"));
    kp->addWidget(kindList_, 1);
    connect(kindList_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const QString kind = item->data(Qt::UserRole).toString();
        if (kind.isEmpty())
            showGlobalFiles(store_.recentlyPlayed(0).isEmpty() ? store_.favorites() : store_.recentlyPlayed(50), QStringLiteral("全部（按最近）"));
        else
            showGlobalFiles(store_.filesWithKind(kind), QStringLiteral("大类 · %1").arg(item->text()));
    });
    sideStack_->addWidget(kindPage);

    // ---- 页 3：分类（facet）+ 自定义标签 ----
    auto *catPage = new QWidget();
    auto *cl = new QVBoxLayout(catPage);
    cl->setContentsMargins(8, 8, 8, 8);
    cl->setSpacing(6);
    auto *catTitle = new QLabel(QStringLiteral("多维分类"), catPage);
    catTitle->setObjectName(QStringLiteral("SideSection"));
    cl->addWidget(catTitle);
    auto *facetHint = new QLabel(
        QStringLiteral("同一素材可从多个维度叠加标注。点维度名展开 / 收起，点标签筛选；"
                       "右键可新建、重命名、删除维度与标签。"),
        catPage);
    facetHint->setObjectName(QStringLiteral("AppSub"));
    facetHint->setWordWrap(true);
    cl->addWidget(facetHint);
    facetTree_ = new QTreeWidget(catPage);
    facetTree_->setHeaderHidden(true);
    facetTree_->setObjectName(QStringLiteral("SideTree"));
    facetTree_->setExpandsOnDoubleClick(false);
    facetTree_->setIndentation(18);
    facetTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    cl->addWidget(facetTree_, 1);
    connect(facetTree_, &QTreeWidget::customContextMenuRequested, this,
            &MainWindow::showFacetContextMenu);
    connect(facetTree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int) {
        // 维度（有子项）→ 点名字展开/收起
        if (item->childCount() > 0) {
            item->setExpanded(!item->isExpanded());
            return;
        }
        const QString ref = item->data(0, Qt::UserRole).toString();
        if (ref.startsWith(QStringLiteral("facet:"))) {
            const QString body = ref.mid(6);
            facetFilterDim_ = body.section(QLatin1Char(':'), 0, 0);
            facetFilterTag_ = body.section(QLatin1Char(':'), 1, 1);
            showGlobalFiles(store_.filesWithFacet(facetFilterDim_, facetFilterTag_),
                            QStringLiteral("分类 · %1").arg(item->text(0)));
        }
    });
    auto *tagTitle = new QLabel(QStringLiteral("自定义标签"), catPage);
    tagTitle->setObjectName(QStringLiteral("SideSection"));
    cl->addWidget(tagTitle);
    tagList_ = new QListWidget(catPage);
    tagList_->setObjectName(QStringLiteral("SideTree"));
    cl->addWidget(tagList_, 1);
    connect(tagList_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const qint64 tagId = item->data(Qt::UserRole).toLongLong();
        showGlobalFiles(store_.filesWithTag(tagId), QStringLiteral("标签 · %1").arg(item->text()));
    });
    sideStack_->addWidget(catPage);

    // ---- 页 4：收藏 ----
    auto *favPage = new QWidget();
    auto *fl = new QVBoxLayout(favPage);
    fl->setContentsMargins(8, 8, 8, 8);
    fl->setSpacing(6);
    auto *favHead = new QHBoxLayout();
    auto *favTitle = new QLabel(QStringLiteral("收藏"), favPage);
    favTitle->setObjectName(QStringLiteral("SideSection"));
    favHead->addWidget(favTitle);
    favHead->addStretch(1);
    auto *addGrp = new QToolButton(favPage);
    addGrp->setObjectName(QStringLiteral("IconBtn"));
    addGrp->setProperty("iconBody", QString::fromLatin1(Icons::Path::Plus));
    addGrp->setIcon(Icons::make(Icons::Path::Plus, Theme::colors(false).muted));
    addGrp->setToolTip(QStringLiteral("新建收藏分组"));
    connect(addGrp, &QToolButton::clicked, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("新建收藏分组"),
                                                   QStringLiteral("分组名："), QLineEdit::Normal,
                                                   QString(), &ok);
        if (ok && !name.trimmed().isEmpty())
            store_.createFavGroup(name.trimmed());
        refreshFavGroups();
    });
    favHead->addWidget(addGrp);
    fl->addLayout(favHead);
    favGroupList_ = new QListWidget(favPage);
    favGroupList_->setObjectName(QStringLiteral("SideTree"));
    fl->addWidget(favGroupList_, 1);
    connect(favGroupList_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const qint64 id = item->data(Qt::UserRole).toLongLong();
        if (id == -1)
            showGlobalFiles(store_.favorites(), QStringLiteral("全部收藏"));
        else
            showGlobalFiles(store_.favGroupItems(id), QStringLiteral("收藏组 · %1").arg(item->text()));
    });
    connect(favGroupList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        const qint64 id = item->data(Qt::UserRole).toLongLong();
        if (id <= 0)
            return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("重命名分组"),
                                                   QStringLiteral("分组名："), QLineEdit::Normal,
                                                   item->text(), &ok);
        if (ok && !name.trimmed().isEmpty())
            store_.renameFavGroup(id, name.trimmed());
        refreshFavGroups();
    });
    sideStack_->addWidget(favPage);

    // ---- 页 5：历史 ----
    auto *histPage = new QWidget();
    auto *hl = new QVBoxLayout(histPage);
    hl->setContentsMargins(8, 8, 8, 8);
    hl->setSpacing(6);
    auto *histTitle = new QLabel(QStringLiteral("最近播放"), histPage);
    histTitle->setObjectName(QStringLiteral("SideSection"));
    hl->addWidget(histTitle);
    historyList_ = new QListWidget(histPage);
    historyList_->setObjectName(QStringLiteral("SideTree"));
    hl->addWidget(historyList_, 1);
    connect(historyList_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty() && QFileInfo::exists(path))
            playPath(path);
    });
    sideStack_->addWidget(histPage);
}

void MainWindow::createCentral()
{
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *splitter = new QSplitter(Qt::Horizontal, central);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(3);

    auto *rail = new QWidget(splitter);
    rail->setObjectName(QStringLiteral("NavRail"));
    rail->setFixedWidth(52);
    auto *rl = new QVBoxLayout(rail);
    rl->setContentsMargins(0, 10, 0, 10);
    rl->setSpacing(4);
    buildNavRail(rail, rl);
    rl->addStretch(1);
    splitter->addWidget(rail);

    sideStack_ = new QStackedWidget(splitter);
    sideStack_->setObjectName(QStringLiteral("SideBar"));
    sideStack_->setMinimumWidth(200);
    sideStack_->setMaximumWidth(320);
    buildSidePanels();
    splitter->addWidget(sideStack_);

    filePanel_ = new FileListPanel(splitter);

    detailPanel_ = new DetailPanel(splitter);
    detailPanel_->setObjectName(QStringLiteral("DetailPane"));
    detailPanel_->setMinimumWidth(260);
    detailPanel_->setMaximumWidth(400);
    detailPanel_->setVisible(false);

    splitter->addWidget(filePanel_);
    splitter->addWidget(detailPanel_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 0);
    splitter->setStretchFactor(2, 1);
    splitter->setStretchFactor(3, 0);
    splitter->setSizes({52, 220, 900, 0});
    layout->addWidget(splitter, 1);

    playerPanel_ = new PlayerPanel(central);
    layout->addWidget(playerPanel_);

    setCentralWidget(central);

    statusCountLabel_ = new QLabel(QString(), this);
    statusBar()->addPermanentWidget(statusCountLabel_);
    statusDeviceLabel_ = new QLabel(QString(), this);
    statusBar()->addPermanentWidget(statusDeviceLabel_);
    agentStatusLabel_ = new QLabel(QString(), this);
    statusBar()->addPermanentWidget(agentStatusLabel_);
}

void MainWindow::createMenus()
{
    auto *fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));

    auto *openAct = fileMenu->addAction(QStringLiteral("打开文件夹…"));
    connect(openAct, &QAction::triggered, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("打开文件夹"));
        if (!dir.isEmpty())
            scanFolder(dir);
    });

    auto *cacheAct = fileMenu->addAction(QStringLiteral("设置缓存文件夹…"));
    connect(cacheAct, &QAction::triggered, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(
            this, QStringLiteral("选择缓存文件夹（标签/分类/收藏/波形存放处）"));
        if (dir.isEmpty())
            return;
        if (!store_.open(dir)) {
            QMessageBox::warning(this, QStringLiteral("缓存文件夹"),
                                 QStringLiteral("无法在所选位置创建缓存：%1").arg(dir));
            return;
        }
        QSettings().setValue(QStringLiteral("cache/dir"), dir);
        statusBar()->showMessage(QStringLiteral("缓存文件夹已切换：%1").arg(dir), 8000);
        if (!currentFolder_.isEmpty())
            scanFolder(currentFolder_);
    });

    auto *refreshAct = fileMenu->addAction(QStringLiteral("刷新（F5）"));
    connect(refreshAct, &QAction::triggered, this, [this] {
        if (!currentFolder_.isEmpty())
            scanFolder(currentFolder_);
    });

    auto *reanalyzeAct = fileMenu->addAction(QStringLiteral("重新分析当前文件夹的波形/频谱"));
    connect(reanalyzeAct, &QAction::triggered, this, [this] {
        if (currentFiles_.isEmpty())
            return;
        for (const FileRecord &r : currentFiles_)
            store_.clearAnalysisCache(r.path);
        statusBar()->showMessage(
            QStringLiteral("已清除 %1 个文件的缓存，正在重新分析…").arg(currentFiles_.size()), 4000);
        scanFolder(currentFolder_);
    });

    fileMenu->addSeparator();
    auto *quitAct = fileMenu->addAction(QStringLiteral("退出(&Q)"));
    connect(quitAct, &QAction::triggered, qApp, &QApplication::quit);

    auto *helpMenu = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    auto *aboutAct = helpMenu->addAction(QStringLiteral("关于 声库"));
    connect(aboutAct, &QAction::triggered, this, [this] {
        QMessageBox::about(this, QStringLiteral("关于 声库"),
            QStringLiteral("声库 v%1\n\n音频素材工作站（资源管理器直读版）\n\n"
                           "· 资源管理器式浏览，点文件夹即听，无导入\n"
                           "· UCS 分类（839 CatID）自动识别打标 + 业务大类（环境/拟声/特殊）\n"
                           "· 多维分类 / 自定义标签 / 收藏分组 / 评分 / 备注 / 历史\n"
                           "· Audition 风格波形 + 频谱图\n"
                           "· 缓存：%2\n"
                           "· AI Agent 接口：127.0.0.1:%3")
                .arg(Theme::appVersion(), store_.cacheDir(),
                     agentServer_ && agentServer_->isRunning()
                         ? QString::number(agentServer_->port()) : QStringLiteral("8618")));
    });
}

void MainWindow::applyTheme(bool dark)
{
    dark_ = dark;
    const ThemeColors c = Theme::colors(dark);
    qApp->setStyleSheet(Theme::styleSheet(dark));

    playerPanel_->setThemeColors(c);
    filePanel_->setThemeColors(c);
    detailPanel_->setThemeColors(c);

    const QColor ic = c.muted;
    const auto buttons = findChildren<QToolButton *>();
    for (auto *b : buttons) {
        const QString body = b->property("iconBody").toString();
        if (!body.isEmpty())
            b->setIcon(Icons::make(body.toLatin1().constData(), ic));
    }
    themeToggle_->setIcon(Icons::make(dark ? Icons::Path::Sun : Icons::Path::Moon, ic));
    themeToggle_->setChecked(dark);
    themeToggle_->setToolTip(dark ? QStringLiteral("切换亮色主题") : QStringLiteral("切换暗色主题"));
}

// ---------------------------------------------------------------- 文件夹与列表

void MainWindow::addLibraryFlow()
{
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("打开文件夹"));
    if (dir.isEmpty())
        return;
    store_.addLibrary(dir);
    scanFolder(dir);
}

void MainWindow::scanFolder(const QString &folder)
{
    analysisGen_++;
    currentFolder_ = folder;
    globalView_ = false;
    tagFilter_ = -1;
    facetFilterDim_.clear();
    facetFilterTag_.clear();
    QSettings().setValue(QStringLiteral("ui/lastFolder"), folder);

    QVector<FileRecord> files;
    if (includeSubdirs_) {
        QDirIterator it(folder, audioNameFilters(), QDir::Files | QDir::Readable,
                        QDirIterator::Subdirectories);
        while (it.hasNext() && files.size() < kMaxFilesPerScan) {
            const QFileInfo fi(it.next());
            FileRecord r;
            r.path = fi.absoluteFilePath();
            r.name = fi.fileName();
            r.relPath = fi.fileName();
            r.size = fi.size();
            r.mtime = fi.lastModified();
            r.format = fi.suffix().toUpper();
            files.append(r);
        }
    } else {
        const auto entries = QDir(folder).entryInfoList(
            audioNameFilters(), QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &fi : entries) {
            FileRecord r;
            r.path = fi.absoluteFilePath();
            r.name = fi.fileName();
            r.relPath = fi.fileName();
            r.size = fi.size();
            r.mtime = fi.lastModified();
            r.format = fi.suffix().toUpper();
            files.append(r);
        }
    }

    store_.loadInto(files);
    currentFiles_ = files;

    // UCS 自动识别打标（仅对未打标的文件；自动识别=暗色显示）
    int ucsHits = 0;
    for (FileRecord &r : currentFiles_) {
        if (r.ucsCat.isEmpty()) {
            const QString catId = store_.detectUcs(r.name);
            if (!catId.isEmpty()) {
                store_.setUcs(r.path, catId, true);
                r.ucsCat = catId;
                r.ucsAuto = true;
                ++ucsHits;
            }
        }
    }
    if (ucsHits > 0)
        statusBar()->showMessage(QStringLiteral("UCS 自动识别 %1 个文件").arg(ucsHits), 4000);

    refilter();
    scheduleAnalysis();
}

void MainWindow::refilter()
{
    QString tagName;
    if (tagFilter_ > 0) {
        for (const TagInfo &t : store_.tags())
            if (t.id == tagFilter_) { tagName = t.name; break; }
    }
    QVector<FileRecord> shown;
    shown.reserve(currentFiles_.size());
    for (const FileRecord &r : currentFiles_) {
        if (tagFilter_ == -2 && !r.favorite)
            continue;
        if (tagFilter_ > 0 && (tagName.isEmpty() || !r.tags.contains(tagName)))
            continue;
        if (!searchText_.isEmpty()) {
            const QString &t = searchText_;
            const bool hit = r.name.contains(t, Qt::CaseInsensitive)
                       || r.relPath.contains(t, Qt::CaseInsensitive)
                       || r.ucsCat.contains(t, Qt::CaseInsensitive)
                       || r.ucsNameZh.contains(t)
                       || r.tags.join(QLatin1Char(' ')).contains(t, Qt::CaseInsensitive);
            if (!hit)
                continue;
        }
        shown.append(r);
    }
    filePanel_->setFiles(shown);
    if (statsLabel_)
        statsLabel_->setText(QStringLiteral("共 %1 个素材").arg(shown.size()));
    if (statusCountLabel_)
        statusCountLabel_->setText(QStringLiteral("共 %1 个素材").arg(shown.size()));
}

void MainWindow::showGlobalFiles(const QVector<FileRecord> &files, const QString &title)
{
    globalView_ = true;
    filePanel_->setFiles(files);
    if (statsLabel_)
        statsLabel_->setText(QStringLiteral("%1 · %2 个素材").arg(title).arg(files.size()));
    if (statusCountLabel_)
        statusCountLabel_->setText(QStringLiteral("%1 · %2 个素材").arg(title).arg(files.size()));
    statusBar()->showMessage(QStringLiteral("全库视图 · %1").arg(title), 4000);
}

void MainWindow::refreshTagChips()
{
    filePanel_->setTags(store_.tags());
    // 侧栏标签列表
    if (tagList_) {
        tagList_->clear();
        for (const TagInfo &t : store_.tags()) {
            auto *item = new QListWidgetItem(
                QStringLiteral("%1 (%2)").arg(t.name).arg(t.count));
            item->setData(Qt::UserRole, t.id);
            item->setForeground(QColor(t.color));
            tagList_->addItem(item);
        }
    }
}

void MainWindow::openTagManager()
{
    auto reload = [this] {
        refreshTagChips();
        if (currentFile_.path.isEmpty())
            return;
        store_.loadInto(currentFile_);
        refreshCurrentDetail();
    };

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("标签管理器"));
    dlg.resize(520, 460);
    auto *lay = new QVBoxLayout(&dlg);

    auto *filter = new QLineEdit(&dlg);
    filter->setPlaceholderText(QStringLiteral("搜索标签…"));
    lay->addWidget(filter);

    auto *list = new QListWidget(&dlg);
    connect(filter, &QLineEdit::textChanged, this, [list](const QString &t) {
        for (int i = 0; i < list->count(); ++i)
            list->item(i)->setHidden(
                !t.isEmpty() && !list->item(i)->text().contains(t, Qt::CaseInsensitive));
    });
    const auto fill = [this, list] {
        list->clear();
        for (const TagInfo &t : store_.tags()) {
            auto *item = new QListWidgetItem(
                QStringLiteral("%1    %2 个素材").arg(t.name).arg(t.count), list);
            item->setData(Qt::UserRole, t.id);
            item->setForeground(QColor(t.color));
        }
    };
    fill();
    lay->addWidget(list, 1);

    // 底部操作按钮
    auto *btnRow = new QHBoxLayout();
    auto *newBtn = new QPushButton(QStringLiteral("新建标签"), &dlg);
    auto *renameBtn = new QPushButton(QStringLiteral("重命名"), &dlg);
    auto *colorBtn = new QPushButton(QStringLiteral("改颜色"), &dlg);
    auto *mergeBtn = new QPushButton(QStringLiteral("合并到…"), &dlg);
    auto *delBtn = new QPushButton(QStringLiteral("删除"), &dlg);
    btnRow->addWidget(newBtn);
    btnRow->addWidget(renameBtn);
    btnRow->addWidget(colorBtn);
    btnRow->addWidget(mergeBtn);
    btnRow->addWidget(delBtn);
    btnRow->addStretch(1);
    auto *closeBtn = new QPushButton(QStringLiteral("关闭"), &dlg);
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    btnRow->addWidget(closeBtn);
    lay->addLayout(btnRow);

    const auto currentTagId = [list]() -> qint64 {
        const auto sel = list->selectedItems();
        return sel.isEmpty() ? 0 : sel.first()->data(Qt::UserRole).toLongLong();
    };

    connect(newBtn, &QPushButton::clicked, this, [this, &dlg, &fill, reload] {
        bool ok = false;
        const QString name = QInputDialog::getText(&dlg, QStringLiteral("新建标签"),
                                                   QStringLiteral("标签名："), QLineEdit::Normal,
                                                   QString(), &ok);
        if (ok && !name.trimmed().isEmpty()) {
            store_.addTag(name.trimmed());
            fill();
            reload();
        }
    });

    connect(renameBtn, &QPushButton::clicked, this, [this, &dlg, currentTagId, &fill, reload] {
        const qint64 id = currentTagId();
        if (id <= 0)
            return;
        QString oldName;
        for (const TagInfo &t : store_.tags())
            if (t.id == id) { oldName = t.name; break; }
        bool ok = false;
        const QString name = QInputDialog::getText(&dlg, QStringLiteral("重命名标签"),
                                                   QStringLiteral("新名称："), QLineEdit::Normal,
                                                   oldName, &ok);
        if (ok && !name.trimmed().isEmpty()) {
            store_.renameTag(id, name.trimmed());
            fill();
            reload();
        }
    });

    connect(colorBtn, &QPushButton::clicked, this, [this, &dlg, currentTagId, &fill, reload] {
        const qint64 id = currentTagId();
        if (id <= 0)
            return;
        const QColor c = QColorDialog::getColor(Qt::white, &dlg, QStringLiteral("选择标签颜色"));
        if (c.isValid()) {
            store_.setTagColor(id, c.name());
            fill();
            reload();
        }
    });

    connect(mergeBtn, &QPushButton::clicked, this, [this, &dlg, currentTagId, &fill, reload] {
        const qint64 fromId = currentTagId();
        if (fromId <= 0)
            return;
        QString fromName;
        QStringList others;
        QVector<qint64> otherIds;
        for (const TagInfo &t : store_.tags()) {
            if (t.id == fromId)
                fromName = t.name;
            else {
                others << t.name;
                otherIds << t.id;
            }
        }
        if (others.isEmpty()) {
            QMessageBox::information(&dlg, QStringLiteral("合并标签"),
                                     QStringLiteral("只有一个标签，无可合并目标。"));
            return;
        }
        bool ok = false;
        const QString target = QInputDialog::getItem(
            &dlg, QStringLiteral("合并标签"),
            QStringLiteral("把「%1」合并到：").arg(fromName), others, 0, false, &ok);
        if (!ok)
            return;
        const qint64 toId = otherIds.at(others.indexOf(target));
        if (QMessageBox::question(&dlg, QStringLiteral("确认合并"),
                QStringLiteral("把「%1」合并到「%2」，然后删除「%1」。继续？")
                    .arg(fromName, target)) != QMessageBox::Yes)
            return;
        store_.mergeTags(fromId, toId);
        fill();
        reload();
    });

    connect(delBtn, &QPushButton::clicked, this, [this, &dlg, currentTagId, &fill, reload] {
        const qint64 id = currentTagId();
        if (id <= 0)
            return;
        QString name;
        int cnt = 0;
        for (const TagInfo &t : store_.tags())
            if (t.id == id) { name = t.name; cnt = t.count; break; }
        if (QMessageBox::question(&dlg, QStringLiteral("删除标签"),
                QStringLiteral("删除标签「%1」？将同时从 %2 个素材上移除。").arg(name).arg(cnt))
            != QMessageBox::Yes)
            return;
        store_.deleteTag(id);
        fill();
        reload();
    });

    dlg.exec();
}

void MainWindow::openBatchTagDialog(const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("批量打标 · %1 个素材").arg(paths.size()));
    dlg.resize(420, 460);
    auto *lay = new QVBoxLayout(&dlg);

    auto *list = new QListWidget(&dlg);
    list->setSelectionMode(QAbstractItemView::MultiSelection);
    for (const TagInfo &t : store_.tags()) {
        auto *item = new QListWidgetItem(
            QStringLiteral("%1  (%2)").arg(t.name).arg(t.count), list);
        item->setData(Qt::UserRole, t.id);
        item->setForeground(QColor(t.color));
    }
    lay->addWidget(list, 1);

    auto *newInput = new QLineEdit(&dlg);
    newInput->setPlaceholderText(QStringLiteral("或输入新标签名，回车即加入并选中"));
    lay->addWidget(newInput);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    btns->button(QDialogButtonBox::Ok)->setText(QStringLiteral("应用到所选素材"));
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(btns);

    // 回车新建标签并选中
    connect(newInput, &QLineEdit::returnPressed, this, [this, &list, newInput] {
        const QString name = newInput->text().trimmed();
        if (name.isEmpty())
            return;
        const qint64 id = store_.addTag(name);
        auto *item = new QListWidgetItem(QStringLiteral("%1  (0)").arg(name), list);
        item->setData(Qt::UserRole, id);
        item->setSelected(true);
        newInput->clear();
    });

    if (dlg.exec() != QDialog::Accepted)
        return;

    // 收集选中标签 id（含对话框内新建的），去重
    QSet<qint64> seen;
    QVector<qint64> ids;
    for (const auto *item : list->selectedItems()) {
        const qint64 id = item->data(Qt::UserRole).toLongLong();
        if (id > 0 && !seen.contains(id)) {
            seen.insert(id);
            ids << id;
        }
    }
    if (ids.isEmpty())
        return;

    // 批量应用
    for (qint64 id : ids)
        for (const QString &p : paths)
            store_.assignTag(p, id);

    // 刷新列表与详情
    for (const QString &p : paths)
        updateFileUserMeta(p);
    refreshTagChips();
    if (!paths.contains(currentFile_.path))
        store_.loadInto(currentFile_);
    statusBar()->showMessage(
        QStringLiteral("已为 %1 个素材批量打标（%2 个标签）").arg(paths.size()).arg(ids.size()), 4000);
}

void MainWindow::refreshFacetTree()
{
    if (!facetTree_)
        return;
    facetTree_->clear();
    for (const FacetDim &d : store_.facets()) {
        auto *dimItem = new QTreeWidgetItem(facetTree_,
            QStringList() << QStringLiteral("%1 (%2)").arg(d.cn).arg(d.tags.size()));
        dimItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        dimItem->setData(0, Qt::UserRole, QStringLiteral("dim:%1").arg(d.dim));
        dimItem->setData(0, Qt::UserRole + 1, d.cn);
        QFont f = dimItem->font(0);
        f.setBold(true);
        dimItem->setFont(0, f);
        for (const FacetTag &t : d.tags) {
            auto *tagItem = new QTreeWidgetItem(dimItem,
                QStringList() << (t.count > 0
                    ? QStringLiteral("%1 (%2)").arg(t.cn).arg(t.count)
                    : t.cn));
            tagItem->setData(0, Qt::UserRole, QStringLiteral("facet:%1:%2").arg(d.dim, t.id));
            tagItem->setData(0, Qt::UserRole + 1, t.cn);
        }
    }
}

void MainWindow::showFacetContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = facetTree_->itemAt(pos);

    // 用户自建维度/标签的唯一 id（时间戳 + 随机数，避免与内置 id 冲突）
    auto genId = []() -> QString {
        const quint64 ts = quint64(QDateTime::currentMSecsSinceEpoch());
        const quint32 rnd = QRandomGenerator::global()->generate();
        return QStringLiteral("u%1%2").arg(ts, 0, 16).arg(rnd, 0, 16);
    };

    // 空白处：新建维度
    if (!item) {
        QMenu menu(this);
        QAction *newDim = menu.addAction(QStringLiteral("新建维度…"));
        QAction *chosen = menu.exec(facetTree_->viewport()->mapToGlobal(pos));
        if (chosen != newDim)
            return;
        bool ok = false;
        const QString dimCn = QInputDialog::getText(
            this, QStringLiteral("新建维度"),
            QStringLiteral("维度名（例如「地域」「用途」）："), QLineEdit::Normal, QString(), &ok);
        if (!ok || dimCn.trimmed().isEmpty())
            return;
        const QString tagCn = QInputDialog::getText(
            this, QStringLiteral("新建维度"),
            QStringLiteral("该维度的第一个标签名："), QLineEdit::Normal, QString(), &ok);
        if (!ok || tagCn.trimmed().isEmpty())
            return;
        store_.addFacetTag(genId(), dimCn.trimmed(), genId(), tagCn.trimmed());
        refreshFacetTree();
        return;
    }

    const QString ref = item->data(0, Qt::UserRole).toString();

    // 维度项：新建标签 / 重命名维度 / 删除维度
    if (ref.startsWith(QStringLiteral("dim:"))) {
        const QString dim = ref.mid(4);
        const QString dimCn = item->data(0, Qt::UserRole + 1).toString();
        QMenu menu(this);
        QAction *newTag = menu.addAction(QStringLiteral("新建标签…"));
        QAction *renameDim = menu.addAction(QStringLiteral("重命名维度…"));
        QAction *delDim = menu.addAction(QStringLiteral("删除维度"));
        QAction *chosen = menu.exec(facetTree_->viewport()->mapToGlobal(pos));

        if (chosen == newTag) {
            bool ok = false;
            const QString tagCn = QInputDialog::getText(
                this, QStringLiteral("新建标签"),
                QStringLiteral("标签名："), QLineEdit::Normal, QString(), &ok);
            if (ok && !tagCn.trimmed().isEmpty())
                store_.addFacetTag(dim, dimCn, genId(), tagCn.trimmed());
            refreshFacetTree();
        } else if (chosen == renameDim) {
            bool ok = false;
            const QString newCn = QInputDialog::getText(
                this, QStringLiteral("重命名维度"),
                QStringLiteral("维度名："), QLineEdit::Normal, dimCn, &ok);
            if (ok && !newCn.trimmed().isEmpty())
                store_.renameFacetDim(dim, newCn.trimmed());
            refreshFacetTree();
        } else if (chosen == delDim) {
            if (QMessageBox::question(this, QStringLiteral("删除维度"),
                    QStringLiteral("删除维度「%1」及其全部标签？将同时从素材上移除这些标注。")
                        .arg(dimCn)) != QMessageBox::Yes)
                return;
            store_.removeFacetDim(dim);
            refreshFacetTree();
        }
        return;
    }

    // 标签项：重命名 / 删除
    if (ref.startsWith(QStringLiteral("facet:"))) {
        const QString body = ref.mid(6);
        const QString dim = body.section(QLatin1Char(':'), 0, 0);
        const QString tagId = body.section(QLatin1Char(':'), 1, 1);
        const QString tagCn = item->data(0, Qt::UserRole + 1).toString();
        QMenu menu(this);
        QAction *renameTag = menu.addAction(QStringLiteral("重命名标签…"));
        QAction *delTag = menu.addAction(QStringLiteral("删除标签"));
        QAction *chosen = menu.exec(facetTree_->viewport()->mapToGlobal(pos));

        if (chosen == renameTag) {
            bool ok = false;
            const QString newCn = QInputDialog::getText(
                this, QStringLiteral("重命名标签"),
                QStringLiteral("标签名："), QLineEdit::Normal, tagCn, &ok);
            if (ok && !newCn.trimmed().isEmpty())
                store_.renameFacetTag(dim, tagId, newCn.trimmed());
            refreshFacetTree();
        } else if (chosen == delTag) {
            if (QMessageBox::question(this, QStringLiteral("删除标签"),
                    QStringLiteral("删除标签「%1」？将同时从素材上移除。").arg(tagCn))
                != QMessageBox::Yes)
                return;
            store_.removeFacetTag(dim, tagId);
            refreshFacetTree();
        }
    }
}

void MainWindow::refreshUcsTree()
{
    if (!ucsTree_)
        return;
    ucsTree_->clear();
    // 读内置 ucs_tree.json 分组结构
    QFile f(QStringLiteral(":/data/ucs_tree.json"));
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const auto cats = store_.ucsCategories();
    QHash<QString, int> counts;
    for (const UcsCat &u : cats)
        counts.insert(u.id, u.count);

    for (const QJsonValue &gv : doc.array()) {
        const QJsonObject g = gv.toObject();
        auto *grpItem = new QTreeWidgetItem(ucsTree_,
            QStringList() << g.value(QStringLiteral("name")).toString());
        grpItem->setFlags(Qt::ItemIsEnabled);
        QFont f = grpItem->font(0);
        f.setBold(true);
        grpItem->setFont(0, f);
        for (const QJsonValue &pv : g.value(QStringLiteral("cats")).toArray()) {
            const QJsonObject p = pv.toObject();
            const QString pid = p.value(QStringLiteral("id")).toString();
            const QString pzh = p.value(QStringLiteral("zh")).toString();
            const int pcnt = counts.value(pid);
            auto *parentItem = new QTreeWidgetItem(grpItem,
                QStringList() << (pcnt > 0 ? QStringLiteral("%1 (%2)").arg(pzh).arg(pcnt) : pzh));
            parentItem->setData(0, Qt::UserRole, pid);
            for (const QJsonValue &sv : p.value(QStringLiteral("subs")).toArray()) {
                const QJsonObject s = sv.toObject();
                const QString sid = s.value(QStringLiteral("id")).toString();
                const int scnt = counts.value(sid);
                auto *subItem = new QTreeWidgetItem(parentItem,
                    QStringList() << (scnt > 0 ? QStringLiteral("%1 (%2)").arg(s.value(QStringLiteral("zh")).toString()).arg(scnt)
                                                : s.value(QStringLiteral("zh")).toString()));
                subItem->setData(0, Qt::UserRole, sid);
                subItem->setToolTip(0, sid);
            }
        }
    }
}

void MainWindow::refreshKindList()
{
    if (!kindList_)
        return;
    kindList_->clear();
    const struct { QString k, cn; } kinds[] = {
        { QString(), QStringLiteral("全部") },
        { QStringLiteral("environment"), QStringLiteral("环境") },
        { QStringLiteral("foley"), QStringLiteral("拟声") },
        { QStringLiteral("special"), QStringLiteral("特殊") },
    };
    for (const auto &k : kinds) {
        auto *item = new QListWidgetItem(
            k.cn.isEmpty() ? k.cn : QStringLiteral("%1 (%2)").arg(k.cn).arg(
                k.k.isEmpty() ? store_.fileCount() : store_.kindCount(k.k)));
        item->setData(Qt::UserRole, k.k);
        kindList_->addItem(item);
    }
}

void MainWindow::refreshFavGroups()
{
    if (!favGroupList_)
        return;
    favGroupList_->clear();
    auto *all = new QListWidgetItem(QStringLiteral("★ 全部收藏"));
    all->setData(Qt::UserRole, qint64(-1));
    favGroupList_->addItem(all);
    for (const FavGroup &g : store_.favGroups()) {
        auto *item = new QListWidgetItem(QStringLiteral("%1 (%2)").arg(g.name).arg(g.count));
        item->setData(Qt::UserRole, g.id);
        item->setToolTip(QStringLiteral("双击重命名"));
        favGroupList_->addItem(item);
    }
}

void MainWindow::refreshHistory()
{
    if (!historyList_)
        return;
    historyList_->clear();
    for (const FileRecord &f : store_.recentlyPlayed(100)) {
        if (!QFileInfo::exists(f.path))
            continue;
        auto *item = new QListWidgetItem(f.name);
        item->setData(Qt::UserRole, f.path);
        item->setToolTip(f.path);
        historyList_->addItem(item);
    }
}

void MainWindow::updateFileUserMeta(const QString &path)
{
    bool foundInFolder = false;
    for (FileRecord &r : currentFiles_) {
        if (r.path == path) {
            store_.loadInto(r);
            filePanel_->updateRecord(r);
            foundInFolder = true;
            break;
        }
    }
    // 全局视图（UCS/标签/收藏/历史筛出的文件不在当前文件夹内）时，
    // 详情页也必须跟着刷新，否则标签 chip 会停留在旧状态、连点行为错乱。
    if (currentFile_.path == path) {
        store_.loadInto(currentFile_);
        refreshCurrentDetail();
    }
    Q_UNUSED(foundInFolder)
}

void MainWindow::refreshCurrentDetail()
{
    if (!currentFile_.path.isEmpty())
        detailPanel_->showFile(currentFile_, store_.tags(), store_.facets(), store_.ucsCategories());
}

// ---------------------------------------------------------------- 后台分析

void MainWindow::scheduleAnalysis()
{
    if (!store_.isOpen())
        return;
    // 波形与频谱分开判断：老素材已有波形缓存时，仍要补生成频谱
    struct Item { QString path; qint64 size; qint64 mtime; bool needWave; bool needSpec; };
    QVector<Item> todo;
    for (const FileRecord &r : currentFiles_) {
        const qint64 mt = r.mtime.isValid() ? r.mtime.toSecsSinceEpoch() : 0;
        const bool waveOk = store_.audioInfoFresh(r.path, r.size, mt);
        const bool specOk = !r.specCache.isEmpty() && QFileInfo::exists(r.specCache);
        if (!waveOk || !specOk)
            todo.append({r.path, r.size, mt, !waveOk, !specOk});
    }
    analyzedCount_ = 0;
    analyzeTotal_ = todo.size();
    if (todo.isEmpty())
        return;

    analysisGen_++;
    const int gen = analysisGen_;
    statusBar()->showMessage(QStringLiteral("正在后台分析波形/频谱（%1 个文件）…").arg(todo.size()), 3000);

    const QPointer<MainWindow> guard(this);
    QtConcurrent::run([this, guard, gen, todo]() {
        for (const Item &item : todo) {
            if (!guard || gen != analysisGen_.load())
                break;

            // 单遍解码：峰值 + 频谱一次性产出（v1.3 提速，避免老版两遍解码）
            WaveformAnalyzer::Analysis a;
            const bool wantSpec = item.needSpec;
            if (!WaveformAnalyzer::analyzeFull(item.path, a, wantSpec)) {
                QMetaObject::invokeMethod(this, [this, guard, gen] {
                    if (guard && gen == analysisGen_.load())
                        ++analyzedCount_;
                }, Qt::QueuedConnection);
                continue;
            }
            const WaveformAnalyzer::Peaks p = a.peaks;

            // 1) 波形
            if (item.needWave) {
                const QString wc = store_.waveCachePathFor(item.path);
                WaveformAnalyzer::saveCache(wc, p);
                QMetaObject::invokeMethod(this,
                    [this, guard, gen, item, p, wc] {
                        if (!guard || gen != analysisGen_.load())
                            return;
                        onAnalyzed(gen, item.path, item.size, item.mtime,
                                   p.duration, p.sampleRate, p.channels, p.bitDepth, wc);
                    }, Qt::QueuedConnection);
            }

            // 2) 频谱
            if (wantSpec && a.hasSpectrogram) {
                const QString sc = store_.specCachePathFor(item.path);
                WaveformAnalyzer::saveSpectrogram(sc, a.spectrogram);
                QMetaObject::invokeMethod(this,
                    [this, guard, gen, item, sc] {
                        if (!guard || gen != analysisGen_.load())
                            return;
                        store_.setSpecCache(item.path, sc);
                        // 若正在试听该文件，即时刷新频谱
                        if (currentFile_.path == item.path) {
                            currentFile_.specCache = sc;
                            WaveformAnalyzer::Spectrogram loaded;
                            if (WaveformAnalyzer::loadSpectrogram(sc, &loaded))
                                playerPanel_->setSpectrogram(spectrogramToImage(loaded));
                        }
                    }, Qt::QueuedConnection);
            }
        }
    });
}

void MainWindow::onAnalyzed(int gen, const QString &path, qint64 size, qint64 mtime,
                            double duration, int sampleRate, int channels, int bitDepth,
                            const QString &waveCache)
{
    Q_UNUSED(gen);
    store_.upsertAudioInfo(path, size, mtime, duration, sampleRate, channels, bitDepth,
                           waveCache);
    for (FileRecord &r : currentFiles_) {
        if (r.path != path)
            continue;
        r.duration = duration;
        r.sampleRate = sampleRate;
        r.channels = channels;
        r.bitDepth = bitDepth;
        r.waveCache = waveCache;
        filePanel_->updateRecord(r);
        if (currentFile_.path == path) {
            currentFile_ = r;
            playerPanel_->setDuration(duration);
            WaveformAnalyzer::Peaks pk;
            if (WaveformAnalyzer::loadCache(waveCache, &pk))
                playerPanel_->setPeaks(pk.coarse, pk.fine, pk.fineL, pk.fineR);
            refreshCurrentDetail();
        }
        break;
    }
    ++analyzedCount_;
    if (analyzedCount_ % 5 == 0 || analyzedCount_ == analyzeTotal_)
        statusBar()->showMessage(
            QStringLiteral("波形分析 %1/%2").arg(analyzedCount_).arg(analyzeTotal_), 2000);
}

// ---------------------------------------------------------------- 播放

void MainWindow::onFileSelected(const FileRecord &rec)
{
    currentFile_ = rec;
    detailPanel_->showFile(rec, store_.tags(), store_.facets(), store_.ucsCategories());

    const QString meta = QStringLiteral("%1 · %2 Hz · %3声道 · %4")
        .arg(rec.format,
             rec.sampleRate > 0 ? QString::number(rec.sampleRate) : QStringLiteral("?"),
             rec.channels > 0 ? QString::number(rec.channels) : QStringLiteral("?"),
             fmtBytes(rec.size));
    playerPanel_->setTrackInfo(rec.name, meta);

    if (!engine_->loadFile(rec.path))
        return;
    store_.recordPlay(rec.path);
    refreshHistory();

    QVector<float> coarse, fine, fineL, fineR;
    WaveformAnalyzer::Peaks pk;
    if (!rec.waveCache.isEmpty() && WaveformAnalyzer::loadCache(rec.waveCache, &pk)) {
        coarse = pk.coarse;
        fine = pk.fine;
        fineL = pk.fineL;
        fineR = pk.fineR;
        playerPanel_->setDuration(pk.duration);
    } else {
        playerPanel_->setDuration(rec.duration);
    }
    playerPanel_->setPeaks(coarse, fine, fineL, fineR);

    // 频谱图（Audition 风格）
    QImage spec;
    WaveformAnalyzer::Spectrogram sp;
    if (!rec.specCache.isEmpty() && WaveformAnalyzer::loadSpectrogram(rec.specCache, &sp))
        spec = spectrogramToImage(sp);
    playerPanel_->setSpectrogram(spec);

    playerPanel_->setPlayPosition(0);
    playerPanel_->setState(false, true);
    engine_->play();
}

void MainWindow::playPath(const QString &path)
{
    FileRecord r;
    r.path = path;
    r.name = QFileInfo(path).fileName();
    r.format = QFileInfo(path).suffix().toUpper();
    r.size = QFileInfo(path).size();
    store_.loadInto(r);
    onFileSelected(r);
}

QString MainWindow::exportCurrentSegment(double t0, double t1)
{
    if (currentFile_.path.isEmpty() || t1 <= t0)
        return QString();
    const QString dir = store_.cacheDir() + QStringLiteral("/exports");
    QDir().mkpath(dir);
    const QString base = QFileInfo(currentFile_.path).completeBaseName();
    const QString out = QStringLiteral("%1/%2_%3_%4s.wav")
        .arg(dir, base)
        .arg(t0, 0, 'f', 1)
        .arg(t1, 0, 'f', 1);
    if (WaveformAnalyzer::exportSegment(currentFile_.path, t0, t1, out)) {
        statusBar()->showMessage(
            QStringLiteral("已导出选区：%1（可拖入宿主软件）").arg(QFileInfo(out).fileName()), 5000);
        return out;
    }
    statusBar()->showMessage(QStringLiteral("选区导出失败"), 3000);
    return QString();
}

void MainWindow::onTogglePlay()
{
    if (engine_->currentPath().isEmpty())
        return;
    if (engine_->state() == AudioEngine::State::Playing)
        engine_->pause();
    else
        engine_->play();
}
