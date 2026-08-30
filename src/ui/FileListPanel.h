#pragma once

#include <QWidget>
#include <QVector>

#include "db/MetadataStore.h"
#include "ui/Theme.h"

class QStackedWidget;
class QTableView;
class QListView;
class QAbstractItemView;
class CardDelegate;
class CompactDelegate;
class WaveColumnDelegate;

/**
 * 中央文件区（v0.9）：文件夹直读模式。
 *  - 顶部筛选 chips（全部 / 仅收藏 / 标签）
 *  - 三种视图：高密度列表 / 音频卡片 / 紧凑波形列表
 *  - 单击试听、空格播放/暂停、可拖出文件到 DAW/剪辑软件
 *  - 后台波形分析完成后按路径单条刷新（不重置选择）
 */
class FileListPanel : public QWidget
{
    Q_OBJECT

public:
    explicit FileListPanel(QWidget *parent = nullptr);

    void setFiles(const QVector<FileRecord> &files);
    void setTags(const QVector<TagInfo> &tags);
    void setViewMode(int mode);          // 0 列表 / 1 卡片 / 2 紧凑波形
    void setThemeColors(const ThemeColors &c);
    bool stepSelection(int delta);       // 上下移动选择（自动试听）；返回是否移动成功
    const QVector<FileRecord> &records() const { return records_; }
    void updateRecord(const FileRecord &rec);   // 波形/属性分析完成后单条刷新

signals:
    void fileSelected(const FileRecord &rec);
    void togglePlayRequested();
    void tagFilterSelected(qint64 tagId);   // -1 = 全部，-2 = 仅收藏
    void favoriteToggled(const QString &path, bool favorite);
    void rootFolderRequested();             // 空状态按钮
    void rootFolderDropped(const QString &path);  // 拖入文件夹 → 切换素材根
    void fileDropped(const QString &path);  // 拖入单个音频文件 → 直接试听
    void batchTagRequested(const QStringList &paths);  // 右键多选批量打标

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    QWidget *makeFilterRow();
    QWidget *makeTableView();
    QWidget *makeCardView();
    QWidget *makeCompactView();
    void wireView(QAbstractItemView *view);
    void connectSelection(QAbstractItemView *view);
    void updateEmptyState();
    QStringList selectedPaths(QAbstractItemView *view) const;

    QVector<FileRecord> records_;

    QStackedWidget *stack_ = nullptr;
    QWidget *emptyPage_ = nullptr;
    QWidget *filterRow_ = nullptr;
    QTableView *table_ = nullptr;
    QListView *cards_ = nullptr;
    QListView *compact_ = nullptr;
    CardDelegate *cardDelegate_ = nullptr;
    CompactDelegate *compactDelegate_ = nullptr;
    WaveColumnDelegate *waveDelegate_ = nullptr;
    qint64 activeTag_ = -1;
    int currentViewMode_ = 0;
    ThemeColors theme_;
    bool headerRestored_ = false;
};
