#include "ui/FileListPanel.h"

#include <QStackedWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableView>
#include <QListView>
#include <QLabel>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QSortFilterProxyModel>
#include <QHeaderView>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QEvent>
#include <QKeyEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QCache>
#include <QSettings>
#include <QPushButton>
#include <QApplication>
#include <QFileInfo>
#include <QMenu>
#include <QAction>
#include <QSet>

#include "audio/WaveformAnalyzer.h"

namespace {

constexpr int RoleRecord = Qt::UserRole + 1;
constexpr int RoleSort   = Qt::UserRole + 2;

QString fmtDur(double sec)
{
    const int s = int(sec);
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

QString fmtSize(qint64 bytes)
{
    if (bytes <= 0)
        return QString();
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024LL * 1024 * 1024)
        return QStringLiteral("%1 MB").arg(bytes / 1024.0 / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(bytes / 1024.0 / 1024.0 / 1024.0, 0, 'f', 2);
}

// 迷你波形内存缓存（键 = 磁盘缓存路径）
QCache<QString, QVector<float>> s_peakCache(4096);

const QVector<float> *coarsePeaks(const QString &cachePath)
{
    if (cachePath.isEmpty())
        return nullptr;
    if (auto *cached = s_peakCache.object(cachePath))
        return cached;
    auto *peaks = new QVector<float>();
    WaveformAnalyzer::Peaks p;
    if (WaveformAnalyzer::loadCache(cachePath, &p))
        *peaks = p.coarse;
    else
        peaks->clear();
    s_peakCache.insert(cachePath, peaks);
    return peaks->isEmpty() ? nullptr : peaks;
}

void drawMiniWave(QPainter *p, const QRectF &r, const QVector<float> *peaks,
                  const QColor &line, const QColor &playedLine)
{
    if (!peaks || peaks->isEmpty() || r.width() < 8)
        return;
    const double cy = r.center().y();
    const int n = qMin(96, peaks->size());
    for (int i = 0; i < n; ++i) {
        double x = r.left() + r.width() * i / qMax(1, n - 1);
        double v = qBound(0.02, double(peaks->at(i * peaks->size() / n)), 1.0);
        double h = v * r.height() * 0.42;
        p->setPen(QPen(i < n * 35 / 100 ? playedLine : line, 1.2));
        p->drawLine(QPointF(x, cy - h), QPointF(x, cy + h));
    }
}

} // namespace

// ---- 卡片 delegate（主题化配色） ----
class CardDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit CardDelegate(const ThemeColors &theme, QObject *parent = nullptr)
        : QStyledItemDelegate(parent), theme_(theme) {}
    void setTheme(const ThemeColors &c) { theme_ = c; }
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    { return QSize(206, 112); }

    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = opt.rect.adjusted(3, 3, -3, -3);
        const FileRecord rec = idx.data(RoleRecord).value<FileRecord>();
        const bool hover = opt.state & QStyle::State_MouseOver;
        const bool selected = opt.state & QStyle::State_Selected;

        QColor bg = selected ? theme_.selection : (hover ? theme_.surfaceAlt : theme_.surface);
        p->setPen(QPen(selected ? theme_.accent : theme_.border, selected ? 1.5 : 1.0));
        p->setBrush(bg);
        p->drawRoundedRect(r, 9, 9);

        // 格式角标
        QRectF badge(r.left() + 8, r.top() + 8, 44, 18);
        const QColor bc = (rec.format == QStringLiteral("WAV")) ? theme_.ok : theme_.warn;
        QColor badgeBg = bc;
        badgeBg.setAlpha(28);
        p->setPen(Qt::NoPen);
        p->setBrush(badgeBg);
        p->drawRoundedRect(badge, 5, 5);
        p->setPen(bc);
        p->setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9, QFont::DemiBold));
        p->drawText(badge, Qt::AlignCenter, rec.format);

        // 名称
        p->setPen(theme_.text);
        p->setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10, QFont::Medium));
        p->drawText(QRectF(r.left() + 8, r.top() + 32, r.width() - 16, 18),
                    Qt::AlignLeft | Qt::AlignVCenter,
                    p->fontMetrics().elidedText(rec.name, Qt::ElideMiddle, int(r.width()) - 16));

        // 指标
        p->setPen(theme_.muted);
        p->setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 8.5));
        const QString meta = QStringLiteral("%1 · %2声道 · %3 Hz")
            .arg(fmtDur(rec.duration))
            .arg(rec.channels > 0 ? QString::number(rec.channels) : QStringLiteral("?"))
            .arg(rec.sampleRate > 0 ? QString::number(rec.sampleRate) : QStringLiteral("?"));
        p->drawText(QRectF(r.left() + 8, r.top() + 52, r.width() - 16, 14),
                    Qt::AlignLeft | Qt::AlignVCenter, meta);

        // 迷你波形（真实数据，随主题）
        drawMiniWave(p, QRectF(r.left() + 8, r.bottom() - 30, r.width() - 16, 18),
                     coarsePeaks(rec.waveCache), theme_.waveformLine, theme_.waveformPlayed);

        // 星标
        if (rec.favorite) {
            p->setPen(theme_.star);
            p->drawText(QRectF(r.right() - 26, r.top() + 6, 20, 16),
                        Qt::AlignCenter, QStringLiteral("★"));
        }
        p->restore();
    }

private:
    ThemeColors theme_;
};

// ---- 紧凑波形列表 delegate ----
class CompactDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit CompactDelegate(const ThemeColors &theme, QObject *parent = nullptr)
        : QStyledItemDelegate(parent), theme_(theme) {}
    void setTheme(const ThemeColors &c) { theme_ = c; }
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    { return QSize(400, 52); }

    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = opt.rect.adjusted(2, 2, -2, -2);
        const FileRecord rec = idx.data(RoleRecord).value<FileRecord>();
        const bool selected = opt.state & QStyle::State_Selected;
        const bool hover = opt.state & QStyle::State_MouseOver;

        QColor bg = selected ? theme_.selection : (hover ? theme_.surfaceAlt : theme_.surface);
        p->setPen(QPen(selected ? theme_.accent : theme_.border, 1));
        p->setBrush(bg);
        p->drawRoundedRect(r, 8, 8);

        // 波形（真实）
        drawMiniWave(p, QRectF(r.left() + 10, r.top() + 13, r.width() - 220, 24),
                     coarsePeaks(rec.waveCache), theme_.waveformLine, theme_.waveformPlayed);

        // 名称
        p->setPen(theme_.text);
        p->setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10));
        p->drawText(QRectF(r.left() + 10, r.bottom() - 17, 200, 14),
                    Qt::AlignLeft | Qt::AlignVCenter,
                    p->fontMetrics().elidedText(rec.name, Qt::ElideMiddle, 200));

        // 时长 + 格式
        p->setPen(theme_.muted);
        p->setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));
        p->drawText(QRectF(r.right() - 180, r.top() + 8, 170, 14),
                    Qt::AlignRight | Qt::AlignVCenter,
                    QStringLiteral("%1 · %2").arg(fmtDur(rec.duration), rec.format));

        if (rec.favorite) {
            p->setPen(theme_.star);
            p->drawText(QRectF(r.right() - 24, r.top() + 4, 18, 14),
                        Qt::AlignCenter, QStringLiteral("★"));
        }
        p->restore();
    }

private:
    ThemeColors theme_;
};

// ---- 表格波形列 delegate ----
class WaveColumnDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit WaveColumnDelegate(const ThemeColors &theme, QObject *parent = nullptr)
        : QStyledItemDelegate(parent), theme_(theme) {}
    void setTheme(const ThemeColors &c) { theme_ = c; }
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
    {
        const FileRecord rec = idx.data(RoleRecord).value<FileRecord>();
        const QRect r = opt.rect.adjusted(2, 4, -2, -4);
        p->save();
        if (opt.state & QStyle::State_Selected)
            p->fillRect(opt.rect, theme_.selection);
        else if (opt.state & QStyle::State_MouseOver)
            p->fillRect(opt.rect, theme_.surfaceAlt);
        drawMiniWave(p, r, coarsePeaks(rec.waveCache), theme_.waveformLine, theme_.waveformPlayed);
        p->restore();
    }

private:
    ThemeColors theme_;
};

// ---- 支持拖出文件 URL 的模型 ----
class FileItemModel : public QStandardItemModel
{
public:
    using QStandardItemModel::QStandardItemModel;
    QMimeData *mimeData(const QModelIndexList &indexes) const override
    {
        auto *mime = new QMimeData;
        QList<QUrl> urls;
        for (const QModelIndex &idx : indexes) {
            const FileRecord rec = idx.data(RoleRecord).value<FileRecord>();
            if (!rec.path.isEmpty())
                urls << QUrl::fromLocalFile(rec.path);
        }
        mime->setUrls(urls);
        return mime;
    }
    QStringList mimeTypes() const override
    {
        return {QStringLiteral("text/uri-list")};
    }
};

FileListPanel::FileListPanel(QWidget *parent)
    : QWidget(parent)
    , theme_(Theme::colors(false))
{
    setObjectName(QStringLiteral("FileArea"));
    setAcceptDrops(true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    filterRow_ = makeFilterRow();
    layout->addWidget(filterRow_);

    stack_ = new QStackedWidget(this);

    auto *listPage = new QWidget(stack_);
    auto *ll = new QVBoxLayout(listPage);
    ll->setContentsMargins(10, 6, 10, 6);
    ll->addWidget(makeTableView());
    stack_->addWidget(listPage);

    auto *cardPage = new QWidget(stack_);
    auto *cl = new QVBoxLayout(cardPage);
    cl->setContentsMargins(10, 6, 10, 6);
    cl->addWidget(makeCardView());
    stack_->addWidget(cardPage);

    auto *compactPage = new QWidget(stack_);
    auto *pl = new QVBoxLayout(compactPage);
    pl->setContentsMargins(10, 6, 10, 6);
    pl->addWidget(makeCompactView());
    stack_->addWidget(compactPage);

    // 空状态页（文件夹直读模式）
    emptyPage_ = new QWidget(stack_);
    auto *el = new QVBoxLayout(emptyPage_);
    el->setAlignment(Qt::AlignCenter);
    auto *dropHint = new QLabel(QStringLiteral("此文件夹暂无音频文件\n或点击下方按钮选择素材文件夹"), emptyPage_);
    dropHint->setObjectName(QStringLiteral("DropZone"));
    dropHint->setAlignment(Qt::AlignCenter);
    dropHint->setFixedHeight(120);
    QFont hintFont = dropHint->font();
    hintFont.setPointSize(11);
    dropHint->setFont(hintFont);
    el->addSpacing(30);
    el->addWidget(dropHint);
    auto *pickBtn = new QPushButton(QStringLiteral("选择素材文件夹…"), emptyPage_);
    pickBtn->setObjectName(QStringLiteral("PrimaryBtn"));
    pickBtn->setCursor(Qt::PointingHandCursor);
    pickBtn->setFixedWidth(200);
    connect(pickBtn, &QPushButton::clicked, this, &FileListPanel::rootFolderRequested);
    el->addSpacing(18);
    el->addWidget(pickBtn, 0, Qt::AlignCenter);
    el->addStretch(2);
    stack_->addWidget(emptyPage_);

    layout->addWidget(stack_, 1);

    // 初始为空
    stack_->setCurrentIndex(3);

    // 退出时保存表格列宽/排序状态
    connect(qApp, &QApplication::aboutToQuit, this, [this] {
        if (table_ && table_->horizontalHeader()) {
            QSettings s;
            s.setValue(QStringLiteral("ui/tableHeader"),
                       table_->horizontalHeader()->saveState());
        }
    });
}

QWidget *FileListPanel::makeFilterRow()
{
    auto *row = new QWidget(this);
    row->setObjectName(QStringLiteral("FilterRow"));
    auto *l = new QHBoxLayout(row);
    l->setContentsMargins(12, 6, 12, 4);
    l->setSpacing(6);

    const auto addChip = [this, row, l](const QString &text, qint64 tagId) {
        auto *btn = new QToolButton(row);
        btn->setObjectName(QStringLiteral("Pill"));
        btn->setText(text);
        btn->setCheckable(true);
        btn->setChecked(tagId == -1);
        btn->setCursor(Qt::PointingHandCursor);
        connect(btn, &QToolButton::clicked, this, [this, tagId, btn] {
            activeTag_ = tagId;
            const auto btns = filterRow_->findChildren<QToolButton *>();
            for (auto *b : btns)
                b->setChecked(b == btn);
            emit tagFilterSelected(tagId);
        });
        l->addWidget(btn);
    };
    addChip(QStringLiteral("全部"), -1);
    addChip(QStringLiteral("★ 仅收藏"), -2);
    return row;
}

void FileListPanel::setTags(const QVector<TagInfo> &tags)
{
    auto *l = qobject_cast<QHBoxLayout *>(filterRow_->layout());
    if (!l)
        return;
    // 保留「全部」「★ 仅收藏」两个内置 chip
    while (l->count() > 2) {
        QLayoutItem *item = l->takeAt(2);
        delete item->widget();
        delete item;
    }
    for (const TagInfo &t : tags) {
        auto *btn = new QToolButton(filterRow_);
        btn->setObjectName(QStringLiteral("Pill"));
        btn->setText(QStringLiteral("%1 (%2)").arg(t.name).arg(t.count));
        btn->setCheckable(true);
        btn->setChecked(activeTag_ == t.id);
        btn->setCursor(Qt::PointingHandCursor);
        connect(btn, &QToolButton::clicked, this, [this, id = t.id, btn] {
            activeTag_ = id;
            const auto btns = filterRow_->findChildren<QToolButton *>();
            for (auto *b : btns)
                b->setChecked(b == btn);
            emit tagFilterSelected(id);
        });
        l->addWidget(btn);
    }
    l->addStretch(1);
}

void FileListPanel::wireView(QAbstractItemView *view)
{
    view->setDragEnabled(true);
    view->setDragDropMode(QAbstractItemView::DragOnly);
    view->setContextMenuPolicy(Qt::CustomContextMenu);
    view->viewport()->installEventFilter(this);
    connect(view, &QAbstractItemView::customContextMenuRequested, this,
            [this, view](const QPoint &pos) {
        const QStringList paths = selectedPaths(view);
        if (paths.isEmpty())
            return;
        QMenu menu(view);
        QAction *act = menu.addAction(QStringLiteral("批量打标（%1 个）…").arg(paths.size()));
        QAction *chosen = menu.exec(view->viewport()->mapToGlobal(pos));
        if (chosen == act)
            emit batchTagRequested(paths);
    });
}

QStringList FileListPanel::selectedPaths(QAbstractItemView *view) const
{
    QStringList out;
    QSet<QString> seen;
    const auto idxs = view->selectionModel()->selectedIndexes();
    for (const QModelIndex &idx : idxs) {
        const FileRecord rec = idx.data(RoleRecord).value<FileRecord>();
        if (!rec.path.isEmpty() && !seen.contains(rec.path)) {
            seen.insert(rec.path);
            out << rec.path;
        }
    }
    return out;
}

void FileListPanel::connectSelection(QAbstractItemView *view)
{
    connect(view->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex &idx) {
                if (idx.isValid())
                    emit fileSelected(idx.data(RoleRecord).value<FileRecord>());
            });
}

QWidget *FileListPanel::makeTableView()
{
    table_ = new QTableView(this);
    table_->setObjectName(QStringLiteral("FileTable"));
    table_->setAlternatingRowColors(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setShowGrid(false);
    table_->setSortingEnabled(true);
    table_->verticalHeader()->setVisible(false);
    table_->verticalHeader()->setDefaultSectionSize(36);
    table_->horizontalHeader()->setHighlightSections(false);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionsClickable(true);
    waveDelegate_ = new WaveColumnDelegate(theme_, table_);
    table_->setItemDelegateForColumn(1, waveDelegate_);
    // 星标列点击 → 切换收藏
    connect(table_, &QTableView::clicked, this, [this](const QModelIndex &idx) {
        if (idx.column() != 0)
            return;
        const FileRecord rec = idx.data(RoleRecord).value<FileRecord>();
        if (rec.path.isEmpty())
            return;
        emit favoriteToggled(rec.path, !rec.favorite);
    });
    // 列宽拖动后实时记忆
    connect(table_->horizontalHeader(), &QHeaderView::sectionResized, this, [this](int, int, int) {
        QSettings s;
        s.setValue(QStringLiteral("ui/tableHeader"),
                   table_->horizontalHeader()->saveState());
    });
    wireView(table_);
    return table_;
}

QWidget *FileListPanel::makeCardView()
{
    cards_ = new QListView(this);
    cards_->setObjectName(QStringLiteral("CardGrid"));
    cards_->setViewMode(QListView::IconMode);
    cards_->setMovement(QListView::Static);
    cards_->setResizeMode(QListView::Adjust);
    cards_->setUniformItemSizes(true);
    cards_->setSpacing(6);
    cards_->setGridSize(QSize(214, 122));
    cards_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    cardDelegate_ = new CardDelegate(theme_, cards_);
    cards_->setItemDelegate(cardDelegate_);
    wireView(cards_);
    return cards_;
}

QWidget *FileListPanel::makeCompactView()
{
    compact_ = new QListView(this);
    compact_->setObjectName(QStringLiteral("CardGrid"));
    compact_->setMovement(QListView::Static);
    compact_->setResizeMode(QListView::Adjust);
    compact_->setUniformItemSizes(true);
    compact_->setSpacing(4);
    compact_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    compactDelegate_ = new CompactDelegate(theme_, compact_);
    compact_->setItemDelegate(compactDelegate_);
    wireView(compact_);
    return compact_;
}

void FileListPanel::setFiles(const QVector<FileRecord> &files)
{
    records_ = files;

    // ---- 列表视图 ----
    {
        auto *model = new FileItemModel(table_);
        model->setSortRole(RoleSort);
        model->setHorizontalHeaderLabels({QStringLiteral("♡"), QStringLiteral("波形"),
            QStringLiteral("文件名"), QStringLiteral("时长"), QStringLiteral("采样率"),
            QStringLiteral("声道"), QStringLiteral("位深"), QStringLiteral("大小"),
            QStringLiteral("格式"), QStringLiteral("标签")});
        for (const FileRecord &rec : files) {
            auto *star = new QStandardItem(rec.favorite ? QStringLiteral("★") : QStringLiteral("☆"));
            star->setData(QVariant::fromValue(rec), RoleRecord);
            star->setData(rec.favorite ? 1 : 0, RoleSort);
            star->setTextAlignment(Qt::AlignCenter);
            star->setForeground(rec.favorite ? theme_.star : theme_.muted);
            star->setEditable(false);

            auto *wave = new QStandardItem();
            wave->setData(QVariant::fromValue(rec), RoleRecord);
            wave->setData(0, RoleSort);
            wave->setEditable(false);

            auto *name = new QStandardItem(rec.name);
            name->setData(QVariant::fromValue(rec), RoleRecord);
            name->setData(rec.name, RoleSort);
            name->setEditable(false);

            auto *dur = new QStandardItem(fmtDur(rec.duration));
            dur->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            dur->setData(QVariant::fromValue(rec), RoleRecord);
            dur->setData(rec.duration, RoleSort);
            dur->setEditable(false);

            auto *sr = new QStandardItem(rec.sampleRate > 0 ? QString::number(rec.sampleRate) : QStringLiteral("—"));
            sr->setData(QVariant::fromValue(rec), RoleRecord);
            sr->setData(rec.sampleRate, RoleSort);
            sr->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            sr->setEditable(false);

            auto *ch = new QStandardItem(rec.channels > 0 ? QString::number(rec.channels) : QStringLiteral("—"));
            ch->setData(QVariant::fromValue(rec), RoleRecord);
            ch->setData(rec.channels, RoleSort);
            ch->setTextAlignment(Qt::AlignCenter);
            ch->setEditable(false);

            auto *bd = new QStandardItem(rec.bitDepth > 0 ? QString::number(rec.bitDepth) : QStringLiteral("—"));
            bd->setData(QVariant::fromValue(rec), RoleRecord);
            bd->setData(rec.bitDepth, RoleSort);
            bd->setTextAlignment(Qt::AlignCenter);
            bd->setEditable(false);

            auto *sz = new QStandardItem(fmtSize(rec.size));
            sz->setData(QVariant::fromValue(rec), RoleRecord);
            sz->setData(qlonglong(rec.size), RoleSort);
            sz->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            sz->setEditable(false);

            auto *fmt = new QStandardItem(rec.format);
            fmt->setData(QVariant::fromValue(rec), RoleRecord);
            fmt->setData(rec.format, RoleSort);
            fmt->setEditable(false);

            auto *tags = new QStandardItem(rec.tags.join(QStringLiteral("  ")));
            tags->setData(QVariant::fromValue(rec), RoleRecord);
            tags->setData(rec.tags.join(QStringLiteral(" ")), RoleSort);
            tags->setEditable(false);

            model->appendRow({star, wave, name, dur, sr, ch, bd, sz, fmt, tags});
        }
        table_->setModel(model);
        // 列宽：优先恢复用户上次调整的列宽；否则用默认列宽
        QSettings s;
        if (s.contains(QStringLiteral("ui/tableHeader"))) {
            table_->horizontalHeader()->restoreState(
                s.value(QStringLiteral("ui/tableHeader")).toByteArray());
        } else {
            table_->setColumnWidth(0, 34);
            table_->setColumnWidth(1, 150);
            table_->setColumnWidth(3, 70);
            table_->setColumnWidth(4, 80);
            table_->setColumnWidth(5, 50);
            table_->setColumnWidth(6, 55);
            table_->setColumnWidth(7, 80);
            table_->setColumnWidth(8, 55);
            table_->setColumnWidth(9, 150);
        }
        table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        connectSelection(table_);
    }

    // ---- 卡片 / 紧凑视图 ----
    {
        auto *model = new FileItemModel(cards_);
        for (const FileRecord &rec : files) {
            auto *item = new QStandardItem();
            item->setData(QVariant::fromValue(rec), RoleRecord);
            item->setEditable(false);
            model->appendRow(item);
        }
        cards_->setModel(model);
        connectSelection(cards_);

        auto *model2 = new FileItemModel(compact_);
        for (const FileRecord &rec : files) {
            auto *item = new QStandardItem();
            item->setData(QVariant::fromValue(rec), RoleRecord);
            item->setEditable(false);
            model2->appendRow(item);
        }
        compact_->setModel(model2);
        connectSelection(compact_);
    }

    updateEmptyState();
}

void FileListPanel::updateRecord(const FileRecord &rec)
{
    // 更新本地向量
    for (FileRecord &r : records_) {
        if (r.path == rec.path) {
            r = rec;
            break;
        }
    }
    // 更新三个视图里的对应行（不重建模型、不重置选择）
    const auto patch = [&rec](QAbstractItemModel *m, int colCount) {
        if (!m)
            return;
        for (int row = 0; row < m->rowCount(); ++row) {
            const QModelIndex first = m->index(row, 0);
            const FileRecord old = first.data(RoleRecord).value<FileRecord>();
            if (old.path != rec.path)
                continue;
            for (int col = 0; col < colCount; ++col)
                m->setData(m->index(row, col), QVariant::fromValue(rec), RoleRecord);
            m->setData(m->index(row, 0),
                       rec.favorite ? QStringLiteral("★") : QStringLiteral("☆"), Qt::DisplayRole);
            m->setData(m->index(row, 0), rec.favorite ? 1 : 0, RoleSort);
            const int s = int(rec.duration);
            m->setData(m->index(row, 3),
                       QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0')),
                       Qt::DisplayRole);
            m->setData(m->index(row, 3), rec.duration, RoleSort);
            m->setData(m->index(row, 4),
                       rec.sampleRate > 0 ? QString::number(rec.sampleRate) : QStringLiteral("—"),
                       Qt::DisplayRole);
            m->setData(m->index(row, 4), rec.sampleRate, RoleSort);
            m->setData(m->index(row, 5),
                       rec.channels > 0 ? QString::number(rec.channels) : QStringLiteral("—"),
                       Qt::DisplayRole);
            m->setData(m->index(row, 5), rec.channels, RoleSort);
            m->setData(m->index(row, 6),
                       rec.bitDepth > 0 ? QString::number(rec.bitDepth) : QStringLiteral("—"),
                       Qt::DisplayRole);
            m->setData(m->index(row, 6), rec.bitDepth, RoleSort);
            m->setData(m->index(row, 9), rec.tags.join(QStringLiteral("  ")), Qt::DisplayRole);
            return;
        }
    };
    patch(table_->model(), 10);
    patch(cards_->model(), 1);
    patch(compact_->model(), 1);
    table_->viewport()->update();
    cards_->viewport()->update();
    compact_->viewport()->update();
}

void FileListPanel::updateEmptyState()
{
    const bool empty = records_.isEmpty();
    if (empty)
        stack_->setCurrentIndex(3);
    else if (stack_->currentIndex() == 3 || stack_->currentIndex() > 2)
        stack_->setCurrentIndex(currentViewMode_);
}

void FileListPanel::setViewMode(int mode)
{
    // v1.2：只有列表(0) / 卡片(1)，紧凑波形列表已移除
    currentViewMode_ = qBound(0, mode, 1);
    if (records_.isEmpty())
        stack_->setCurrentIndex(3);
    else
        stack_->setCurrentIndex(currentViewMode_);
}

bool FileListPanel::stepSelection(int delta)
{
    QAbstractItemView *view = nullptr;
    switch (stack_->currentIndex()) {
    case 0: view = table_; break;
    case 1: view = cards_; break;
    case 2: view = compact_; break;
    default: return false;
    }
    if (!view || !view->model())
        return false;
    const int rows = view->model()->rowCount();
    if (rows == 0)
        return false;
    QModelIndex cur = view->currentIndex();
    int row = cur.isValid() ? cur.row() : -1;
    row = qBound(0, row + delta, rows - 1);
    if (row == cur.row())      // 已在边界，无法继续移动
        return false;
    view->setCurrentIndex(view->model()->index(row, 0));
    // 显式触发选择（确保 fileSelected 发出）
    const FileRecord rec = view->model()->index(row, 0).data(RoleRecord).value<FileRecord>();
    if (!rec.path.isEmpty())
        emit fileSelected(rec);
    return true;
}

void FileListPanel::setThemeColors(const ThemeColors &c)
{
    theme_ = c;
    if (cardDelegate_)
        cardDelegate_->setTheme(c);
    if (compactDelegate_)
        compactDelegate_->setTheme(c);
    if (waveDelegate_)
        waveDelegate_->setTheme(c);
    table_->viewport()->update();
    cards_->viewport()->update();
    compact_->viewport()->update();
}

bool FileListPanel::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Space || key->key() == Qt::Key_Return
            || key->key() == Qt::Key_Enter) {
            emit togglePlayRequested();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void FileListPanel::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void FileListPanel::dropEvent(QDropEvent *event)
{
    const auto urls = event->mimeData()->urls();
    for (const QUrl &u : urls) {
        if (!u.isLocalFile())
            continue;
        const QString p = u.toLocalFile();
        if (QFileInfo(p).isDir()) {
            emit rootFolderDropped(p);
            event->acceptProposedAction();
            return;
        }
    }
    // 单个音频文件拖入 → 直接试听
    for (const QUrl &u : urls) {
        if (u.isLocalFile() && !u.toLocalFile().isEmpty()) {
            emit fileDropped(u.toLocalFile());
            break;
        }
    }
    event->acceptProposedAction();
}

#include "FileListPanel.moc"
