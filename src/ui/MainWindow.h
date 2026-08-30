#pragma once

#include <QMainWindow>
#include <QVector>
#include <QString>
#include <atomic>

#include "db/MetadataStore.h"
#include "ui/Theme.h"

class AudioEngine;
class AgentService;
class AgentHttpServer;
class FileListPanel;
class DetailPanel;
class PlayerPanel;
class QFileSystemModel;
class QTreeView;
class QListWidget;
class QTreeWidget;
class QLineEdit;
class QLabel;
class QToolButton;
class QComboBox;
class QStackedWidget;
class QCheckBox;
class QTimer;
class QTreeWidgetItem;
class QVBoxLayout;

/**
 * 主窗口（v1.0 · 参考 MATRIX WAVE 布局）
 *
 * 布局：顶栏（搜索/排序/输出设备/设置）| 左侧图标导航轨 | 侧栏多面板
 *       | 中央素材表 | 右侧详情 + 底部波形播放器。
 *
 * 侧栏按导航切换五个「浏览域」：
 *   资料库（多根文件夹直读）/ 分类（多维 facet + 自定义标签）/ 收藏（分组）
 *   / 历史（最近播放·搜索）
 *
 * 数据层：文件系统是唯一事实来源，标签/facet/收藏/评分/波形全在缓存文件夹。
 * Agent：本地 HTTP 接口（127.0.0.1:8618）暴露全部工具给外部 AI Agent。
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    void createTopBar();
    void createCentral();
    void createMenus();
    QToolButton *makeIconButton(const char *svgBody, const QString &tip, bool checkable);

    // 侧栏 / 导航
    void buildNavRail(QWidget *host, QVBoxLayout *hostLayout);
    void buildSidePanels();
    void switchSide(int index);

    // 文件夹 / 列表
    void addLibraryFlow();
    void scanFolder(const QString &folder);
    void refilter();
    void refreshTagChips();
    void refreshFacetTree();
    void showFacetContextMenu(const QPoint &pos);  // 多维分类树右键：新建/重命名/删除维度与标签
    void openTagManager();       // 标签管理器：重命名/改色/删除/合并/批量打标
    void openBatchTagDialog(const QStringList &paths);  // 多选批量打标
    void refreshUcsTree();
    void refreshKindList();
    void refreshFavGroups();
    void refreshHistory();
    void updateFileUserMeta(const QString &path);
    void refreshCurrentDetail();
    void showGlobalFiles(const QVector<FileRecord> &files, const QString &title);

    // 后台分析
    void scheduleAnalysis();
    void onAnalyzed(int gen, const QString &path, qint64 size, qint64 mtime,
                    double duration, int sampleRate, int channels, int bitDepth,
                    const QString &waveCache);

    // 播放
    void onFileSelected(const FileRecord &rec);
    void onTogglePlay();
    void playPath(const QString &path);
    QString exportCurrentSegment(double t0, double t1);   // 选区导出（拖入宿主）

    // 设置 / 主题 / Agent
    void applyTheme(bool dark);
    QString defaultCacheDir() const;
    void repopulateDevices();
    void restoreDeviceSetting();
    void startAgentServer();

    // 顶栏控件
    QLineEdit *searchField_ = nullptr;
    QLabel *statsLabel_ = nullptr;
    QToolButton *listViewBtn_ = nullptr;
    QToolButton *cardViewBtn_ = nullptr;
    QToolButton *detailToggle_ = nullptr;
    QToolButton *themeToggle_ = nullptr;
    QToolButton *refreshBtn_ = nullptr;
    QComboBox *deviceCombo_ = nullptr;
    QLabel *statusCountLabel_ = nullptr;
    QLabel *statusDeviceLabel_ = nullptr;
    QLabel *agentStatusLabel_ = nullptr;

    // 导航轨
    QVector<QToolButton *> navButtons_;

    // 侧栏
    QStackedWidget *sideStack_ = nullptr;
    QFileSystemModel *fsModel_ = nullptr;
    QTreeView *folderTree_ = nullptr;
    QCheckBox *subdirsCheck_ = nullptr;
    QTreeWidget *ucsTree_ = nullptr;
    QListWidget *kindList_ = nullptr;
    QTreeWidget *facetTree_ = nullptr;
    QListWidget *tagList_ = nullptr;
    QListWidget *favGroupList_ = nullptr;
    QListWidget *historyList_ = nullptr;
    QLineEdit *historySearchLabel_ = nullptr;

    // 中央 / 右侧 / 底部
    QStackedWidget *centralStack_ = nullptr;
    FileListPanel *filePanel_ = nullptr;
    DetailPanel *detailPanel_ = nullptr;
    PlayerPanel *playerPanel_ = nullptr;

    // 状态
    AudioEngine *engine_ = nullptr;
    MetadataStore store_;
    AgentService *agent_ = nullptr;
    AgentHttpServer *agentServer_ = nullptr;
    QString rootFolder_;
    QString currentFolder_;
    QVector<FileRecord> currentFiles_;      // 当前文件夹全量（未过滤）
    FileRecord currentFile_;
    qint64 tagFilter_ = -1;                 // -1 全部 -2 仅收藏 >0 标签id
    QString facetFilterDim_;                // facet 筛选维度（空=无）
    QString facetFilterTag_;
    QString searchText_;
    bool includeSubdirs_ = false;
    bool dark_ = false;
    bool contPlay_ = true;                  // 连续播放（播完自动下一个）
    bool globalView_ = false;               // true=显示全库视图（facet/标签/收藏/历史）
    QTimer *searchDebounce_ = nullptr;
    std::atomic<int> analysisGen_{0};
    int analyzedCount_ = 0;
    int analyzeTotal_ = 0;
};
