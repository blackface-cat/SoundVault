#pragma once

#include <QWidget>
#include <QVector>

#include "db/MetadataStore.h"
#include "ui/Theme.h"

class QLabel;
class QLineEdit;
class QTextEdit;
class QToolButton;
class QFormLayout;
class QTimer;
class QScrollArea;
class QComboBox;
class QCompleter;
class QStringListModel;

/**
 * 右侧详情栏（v1.2）：信息 · UCS 分类 · 业务大类 · 多维分类(facet) · 标签 · 收藏/评分 · 备注 · 属性
 *
 * 标签区：输入框带自动补全（复用已有标签，避免同义重复），
 *        已选标签用各自颜色高亮，输入新名字回车即创建。
 */
class DetailPanel : public QWidget
{
    Q_OBJECT

public:
    explicit DetailPanel(QWidget *parent = nullptr);

    void showFile(const FileRecord &rec, const QVector<TagInfo> &allTags,
                  const QVector<FacetDim> &facets, const QVector<UcsCat> &ucsCats = {});
    void clear();
    void setThemeColors(const ThemeColors &c);

signals:
    void favoriteToggled(const QString &path, bool fav);
    void ratingChanged(const QString &path, int rating);
    void notesChanged(const QString &path, const QString &notes);
    void tagAssigned(const QString &path, qint64 tagId);
    void tagRemoved(const QString &path, qint64 tagId);
    void tagCreateRequested(const QString &path, const QString &name);
    void facetToggled(const QString &path, const QString &dim, const QString &tagId, bool on);
    void ucsChanged(const QString &path, const QString &catId);
    void kindChanged(const QString &path, const QString &kind);
    /// 请求打开标签管理器（重命名/删除/改色/合并）
    void tagManagerRequested();

protected:
    void hideEvent(QHideEvent *event) override;

private:
    void rebuildTagChips();
    void rebuildStars();
    void rebuildFacetChips();
    void flushPendingNotes();
    void openFacetDialog(const FacetDim &dim);
    void openUcsDialog();

    FileRecord rec_;
    QVector<TagInfo> allTags_;
    QVector<FacetDim> facets_;
    QVector<UcsCat> ucsCats_;
    ThemeColors theme_;

    QLabel *nameLabel_ = nullptr;
    QLabel *pathLabel_ = nullptr;
    QLabel *ucsLabel_ = nullptr;
    QComboBox *kindCombo_ = nullptr;
    QScrollArea *facetScroll_ = nullptr;
    QWidget *facetRow_ = nullptr;
    QWidget *tagChipRow_ = nullptr;
    QLineEdit *tagInput_ = nullptr;
    QCompleter *tagCompleter_ = nullptr;
    QStringListModel *tagModel_ = nullptr;
    QToolButton *favBtn_ = nullptr;
    QWidget *starRow_ = nullptr;
    QTextEdit *notesEdit_ = nullptr;
    QTimer *notesDebounce_ = nullptr;
    QFormLayout *propLayout_ = nullptr;
};
