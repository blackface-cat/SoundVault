#include "ui/DetailPanel.h"
#include "ui/Icons.h"

#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>
#include <QToolButton>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QScrollArea>
#include <QFrame>
#include <QFileInfo>
#include <QDesktopServices>
#include <QTimer>
#include <QUrl>
#include <QDialog>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QLineEdit>
#include <QCompleter>
#include <QStringListModel>

namespace {
QString fmtTime(double sec)
{
    const int s = int(sec);
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}
}

DetailPanel::DetailPanel(QWidget *parent)
    : QWidget(parent)
    , theme_(Theme::colors(true))
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 12, 12, 12);
    outer->setSpacing(6);

    nameLabel_ = new QLabel(QStringLiteral("未选择素材"), this);
    nameLabel_->setStyleSheet(QStringLiteral("font-size:15px;font-weight:800;letter-spacing:0.3px;"));
    nameLabel_->setWordWrap(true);
    outer->addWidget(nameLabel_);

    pathLabel_ = new QLabel(this);
    pathLabel_->setObjectName(QStringLiteral("AppSub"));
    pathLabel_->setWordWrap(true);
    pathLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    outer->addWidget(pathLabel_);

    auto makeHLine = [this] {
        auto *line = new QFrame(this);
        line->setObjectName(QStringLiteral("HLine"));
        line->setFrameShape(QFrame::NoFrame);
        line->setFixedHeight(1);
        return line;
    };
    outer->addSpacing(2);
    outer->addWidget(makeHLine());
    outer->addSpacing(2);

    // ---- UCS 分类 + 业务大类 ----
    auto *ucsRow = new QHBoxLayout();
    auto *ucsTitle = new QLabel(QStringLiteral("UCS"), this);
    ucsTitle->setObjectName(QStringLiteral("SideSection"));
    ucsRow->addWidget(ucsTitle);
    ucsLabel_ = new QLabel(QStringLiteral("—"), this);
    ucsLabel_->setWordWrap(true);
    ucsRow->addWidget(ucsLabel_, 1);
    auto *ucsEdit = new QToolButton(this);
    ucsEdit->setObjectName(QStringLiteral("Pill"));
    ucsEdit->setText(QStringLiteral("改"));
    ucsEdit->setToolTip(QStringLiteral("手动选择 UCS 分类"));
    connect(ucsEdit, &QToolButton::clicked, this, &DetailPanel::openUcsDialog);
    ucsRow->addWidget(ucsEdit);
    auto *ucsClear = new QToolButton(this);
    ucsClear->setObjectName(QStringLiteral("Pill"));
    ucsClear->setText(QStringLiteral("清"));
    ucsClear->setToolTip(QStringLiteral("清除 UCS 分类"));
    connect(ucsClear, &QToolButton::clicked, this, [this] {
        if (!rec_.path.isEmpty() && !rec_.ucsCat.isEmpty())
            emit ucsChanged(rec_.path, QString());
    });
    ucsRow->addWidget(ucsClear);
    outer->addLayout(ucsRow);

    auto *kindRow = new QHBoxLayout();
    auto *kindTitle = new QLabel(QStringLiteral("大类"), this);
    kindTitle->setObjectName(QStringLiteral("SideSection"));
    kindRow->addWidget(kindTitle);
    kindCombo_ = new QComboBox(this);
    kindCombo_->addItem(QStringLiteral("未设置"), QString());
    kindCombo_->addItem(QStringLiteral("环境"), QStringLiteral("environment"));
    kindCombo_->addItem(QStringLiteral("拟声"), QStringLiteral("foley"));
    kindCombo_->addItem(QStringLiteral("特殊"), QStringLiteral("special"));
    connect(kindCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!rec_.path.isEmpty())
            emit kindChanged(rec_.path, kindCombo_->currentData().toString());
    });
    kindRow->addWidget(kindCombo_);
    kindRow->addStretch(1);
    outer->addLayout(kindRow);

    outer->addSpacing(2);
    outer->addWidget(makeHLine());
    outer->addSpacing(2);

    // ---- 多维分类（facet） ----
    auto *facetHead = new QHBoxLayout();
    auto *facetTitle = new QLabel(QStringLiteral("多维分类"), this);
    facetTitle->setObjectName(QStringLiteral("SideSection"));
    facetHead->addWidget(facetTitle);
    facetHead->addStretch(1);
    outer->addLayout(facetHead);

    facetScroll_ = new QScrollArea(this);
    facetScroll_->setWidgetResizable(true);
    facetScroll_->setFrameShape(QFrame::NoFrame);
    facetScroll_->setMaximumHeight(120);
    facetRow_ = new QWidget(facetScroll_);
    facetRow_->setLayout(new QVBoxLayout());
    facetRow_->layout()->setContentsMargins(0, 0, 0, 0);
    facetRow_->layout()->setSpacing(3);
    facetScroll_->setWidget(facetRow_);
    outer->addWidget(facetScroll_);

    outer->addSpacing(2);
    outer->addWidget(makeHLine());
    outer->addSpacing(2);

    // ---- 标签 ----
    auto *tagHead = new QHBoxLayout();
    auto *tagTitle = new QLabel(QStringLiteral("标签"), this);
    tagTitle->setObjectName(QStringLiteral("SideSection"));
    tagHead->addWidget(tagTitle);
    tagHead->addStretch(1);
    auto *tagMgrBtn = new QToolButton(this);
    tagMgrBtn->setObjectName(QStringLiteral("Pill"));
    tagMgrBtn->setText(QStringLiteral("管理…"));
    tagMgrBtn->setToolTip(QStringLiteral("标签管理器：重命名 / 改色 / 删除 / 合并"));
    connect(tagMgrBtn, &QToolButton::clicked, this, &DetailPanel::tagManagerRequested);
    tagHead->addWidget(tagMgrBtn);
    outer->addLayout(tagHead);

    auto *tagScroll = new QScrollArea(this);
    tagScroll->setWidgetResizable(true);
    tagScroll->setFrameShape(QFrame::NoFrame);
    tagScroll->setMaximumHeight(70);
    tagChipRow_ = new QWidget(tagScroll);
    tagChipRow_->setLayout(new QHBoxLayout());
    tagChipRow_->layout()->setContentsMargins(0, 0, 0, 0);
    tagChipRow_->layout()->setSpacing(4);
    tagScroll->setWidget(tagChipRow_);
    outer->addWidget(tagScroll);

    tagInput_ = new QLineEdit(this);
    tagInput_->setObjectName(QStringLiteral("SearchField"));
    tagInput_->setPlaceholderText(QStringLiteral("输入标签名，回车添加（自动提示已有标签）"));
    // 自动补全：优先复用已有标签，避免 "脚步" / "脚步声" 这类同义重复
    tagModel_ = new QStringListModel(this);
    tagCompleter_ = new QCompleter(tagModel_, this);
    tagCompleter_->setCaseSensitivity(Qt::CaseInsensitive);
    tagCompleter_->setCompletionMode(QCompleter::PopupCompletion);
    tagCompleter_->setFilterMode(Qt::MatchContains);
    tagInput_->setCompleter(tagCompleter_);
    connect(tagInput_, &QLineEdit::returnPressed, this, [this] {
        const QString name = tagInput_->text().trimmed();
        if (name.isEmpty() || rec_.path.isEmpty())
            return;
        for (const TagInfo &t : allTags_) {
            // 大小写不敏感匹配，避免重名变体
            if (t.name.compare(name, Qt::CaseInsensitive) == 0) {
                if (!rec_.tags.contains(t.name))
                    emit tagAssigned(rec_.path, t.id);
                tagInput_->clear();
                return;
            }
        }
        emit tagCreateRequested(rec_.path, name);
        tagInput_->clear();
    });
    outer->addWidget(tagInput_);

    // ---- 收藏 + 评分 ----
    auto *favRow = new QHBoxLayout();
    favBtn_ = new QToolButton(this);
    favBtn_->setObjectName(QStringLiteral("IconBtn"));
    favBtn_->setCursor(Qt::PointingHandCursor);
    connect(favBtn_, &QToolButton::clicked, this, [this] {
        if (!rec_.path.isEmpty())
            emit favoriteToggled(rec_.path, !rec_.favorite);
    });
    favRow->addWidget(favBtn_);
    starRow_ = new QWidget(this);
    auto *starLay = new QHBoxLayout(starRow_);
    starLay->setContentsMargins(0, 0, 0, 0);
    starLay->setSpacing(2);
    for (int i = 1; i <= 5; ++i) {
        auto *b = new QToolButton(starRow_);
        b->setObjectName(QStringLiteral("IconBtn"));
        b->setCheckable(true);
        b->setProperty("star", i);
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QToolButton::clicked, this, [this, i] {
            if (!rec_.path.isEmpty())
                emit ratingChanged(rec_.path, i);
        });
        starLay->addWidget(b);
    }
    favRow->addWidget(starRow_);
    favRow->addStretch(1);
    outer->addLayout(favRow);

    // ---- 备注 ----
    notesEdit_ = new QTextEdit(this);
    notesEdit_->setObjectName(QStringLiteral("SearchField"));
    notesEdit_->setPlaceholderText(QStringLiteral("备注…"));
    notesEdit_->setFixedHeight(52);
    notesDebounce_ = new QTimer(this);
    notesDebounce_->setSingleShot(true);
    notesDebounce_->setInterval(500);
    connect(notesDebounce_, &QTimer::timeout, this, [this] {
        if (!rec_.path.isEmpty())
            emit notesChanged(rec_.path, notesEdit_->toPlainText());
    });
    connect(notesEdit_, &QTextEdit::textChanged, this, [this] {
        if (!rec_.path.isEmpty())
            notesDebounce_->start();
    });
    outer->addWidget(notesEdit_);

    outer->addSpacing(2);
    outer->addWidget(makeHLine());
    outer->addSpacing(2);

    // ---- 属性 ----
    auto *propCard = new QFrame(this);
    propCard->setObjectName(QStringLiteral("SideCard"));
    auto *propCardLay = new QVBoxLayout(propCard);
    propCardLay->setContentsMargins(8, 6, 8, 6);
    propLayout_ = new QFormLayout();
    propLayout_->setHorizontalSpacing(10);
    propLayout_->setVerticalSpacing(3);
    propCardLay->addLayout(propLayout_);
    outer->addWidget(propCard);

    outer->addStretch(1);

    auto *openBtn = new QPushButton(QStringLiteral("在资源管理器中打开"), this);
    openBtn->setObjectName(QStringLiteral("Pill"));
    connect(openBtn, &QPushButton::clicked, this, [this] {
        if (!rec_.path.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(rec_.path).absolutePath()));
    });
    outer->addWidget(openBtn);
}

void DetailPanel::showFile(const FileRecord &rec, const QVector<TagInfo> &allTags,
                           const QVector<FacetDim> &facets, const QVector<UcsCat> &ucsCats)
{
    flushPendingNotes();
    rec_ = rec;
    allTags_ = allTags;
    facets_ = facets;
    ucsCats_ = ucsCats;

    nameLabel_->setText(rec.name.isEmpty() ? QStringLiteral("未选择素材") : rec.name);
    pathLabel_->setText(rec.path);

    // UCS + 大类
    if (ucsLabel_) {
        if (rec.ucsCat.isEmpty()) {
            ucsLabel_->setText(QStringLiteral("—"));
            ucsLabel_->setStyleSheet(QString());
        } else {
            // 自动识别=暗色（系统默认）；手动=主题正文色
            const QString txt = rec.ucsAuto
                ? QStringLiteral("%1 · %2（自动）").arg(rec.ucsCat, rec.ucsNameZh)
                : QStringLiteral("%1 · %2").arg(rec.ucsCat, rec.ucsNameZh);
            ucsLabel_->setText(txt);
            const QColor c = rec.ucsAuto ? theme_.muted : theme_.text;
            ucsLabel_->setStyleSheet(QStringLiteral("color:%1;font-size:12px;").arg(c.name()));
        }
    }
    if (kindCombo_) {
        kindCombo_->blockSignals(true);
        int idx = 0;
        for (int i = 0; i < kindCombo_->count(); ++i)
            if (kindCombo_->itemData(i).toString() == rec.kind)
                idx = i;
        kindCombo_->setCurrentIndex(idx);
        kindCombo_->blockSignals(false);
    }

    favBtn_->setIcon(Icons::make(Icons::Path::Star,
        rec.favorite ? QColor(0xF5, 0xB8, 0x4C) : theme_.muted));
    rebuildStars();
    rebuildTagChips();
    rebuildFacetChips();
    notesEdit_->blockSignals(true);
    notesEdit_->setPlainText(rec.notes);
    notesEdit_->blockSignals(false);

    while (propLayout_->rowCount() > 0)
        propLayout_->removeRow(0);
    const auto addProp = [this](const QString &k, const QString &v) {
        auto *vl = new QLabel(v.isEmpty() ? QStringLiteral("—") : v, this);
        vl->setStyleSheet(QStringLiteral("color:%1;font-size:12px;").arg(theme_.muted.name()));
        vl->setTextInteractionFlags(Qt::TextSelectableByMouse);
        auto *kl = new QLabel(k, this);
        kl->setStyleSheet(QStringLiteral("color:#9AA1AC;font-size:12px;"));
        propLayout_->addRow(kl, vl);
    };
    addProp(QStringLiteral("格式"), rec.format);
    addProp(QStringLiteral("时长"), fmtTime(rec.duration));
    addProp(QStringLiteral("采样率"), rec.sampleRate > 0 ? QStringLiteral("%1 Hz").arg(rec.sampleRate) : QString());
    addProp(QStringLiteral("位数"), rec.bitDepth > 0 ? QStringLiteral("%1 bit").arg(rec.bitDepth) : QString());
    addProp(QStringLiteral("声道"), rec.channels > 0 ? QStringLiteral("%1").arg(rec.channels) : QString());
    addProp(QStringLiteral("大小"), rec.size > 0 ? QStringLiteral("%1 MB").arg(rec.size / 1048576.0, 0, 'f', 1) : QString());
    addProp(QStringLiteral("修改时间"), rec.mtime.isValid()
        ? rec.mtime.toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QString());
    addProp(QStringLiteral("相对路径"), rec.relPath);
}

void DetailPanel::rebuildFacetChips()
{
    if (!facetRow_ || !facetRow_->layout())
        return;
    QLayoutItem *child = nullptr;
    while ((child = facetRow_->layout()->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }
    auto *lay = qobject_cast<QVBoxLayout *>(facetRow_->layout());

    // 已选 facet：按维度分组展示
    QHash<QString, QStringList> sel;   // dim -> [tag_id]
    for (const QString &ref : rec_.facetRefs) {
        const QString dim = ref.section(QLatin1Char(':'), 0, 0);
        const QString tid = ref.section(QLatin1Char(':'), 1, 1);
        sel[dim] << tid;
    }

    for (const FacetDim &d : facets_) {
        auto *row = new QHBoxLayout();
        auto *dimLabel = new QLabel(d.cn, facetRow_);
        dimLabel->setObjectName(QStringLiteral("AppSub"));
        dimLabel->setFixedWidth(44);
        row->addWidget(dimLabel);

        const QStringList ids = sel.value(d.dim);
        const QHash<QString, QString> names = [&] {
            QHash<QString, QString> m;
            for (const FacetTag &t : d.tags)
                m.insert(t.id, t.cn);
            return m;
        }();
        for (const QString &tid : ids) {
            auto *chip = new QToolButton(facetRow_);
            chip->setObjectName(QStringLiteral("Pill"));
            chip->setText(QStringLiteral("%1 ×").arg(names.value(tid, tid)));
            chip->setChecked(true);
            chip->setCursor(Qt::PointingHandCursor);
            connect(chip, &QToolButton::clicked, this, [this, dim = d.dim, tid] {
                emit facetToggled(rec_.path, dim, tid, false);
            });
            row->addWidget(chip);
        }
        auto *add = new QToolButton(facetRow_);
        add->setObjectName(QStringLiteral("Pill"));
        add->setText(QStringLiteral("+"));
        add->setToolTip(QStringLiteral("添加 %1").arg(d.cn));
        add->setCursor(Qt::PointingHandCursor);
        connect(add, &QToolButton::clicked, this, [this, d] { openFacetDialog(d); });
        row->addWidget(add);
        row->addStretch(1);
        lay->addLayout(row);
    }
}

void DetailPanel::openFacetDialog(const FacetDim &dim)
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("选择 %1").arg(dim.cn));
    auto *lay = new QVBoxLayout(&dlg);
    auto *list = new QListWidget(&dlg);
    list->setSelectionMode(QAbstractItemView::MultiSelection);
    for (const FacetTag &t : dim.tags) {
        auto *item = new QListWidgetItem(
            t.group.isEmpty() ? t.cn : QStringLiteral("%1 · %2").arg(t.group, t.cn), list);
        item->setData(Qt::UserRole, t.id);
        if (!t.en.isEmpty())
            item->setToolTip(t.en);
        if (rec_.facetRefs.contains(dim.dim + QLatin1Char(':') + t.id))
            item->setSelected(true);
    }
    lay->addWidget(list);
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(btns);
    if (dlg.exec() != QDialog::Accepted || rec_.path.isEmpty())
        return;

    // 计算差异，仅 emit 变化项
    QSet<QString> wanted;
    for (const auto *item : list->selectedItems())
        wanted.insert(item->data(Qt::UserRole).toString());
    QSet<QString> had;
    for (const QString &ref : rec_.facetRefs) {
        const QString d = ref.section(QLatin1Char(':'), 0, 0);
        const QString t = ref.section(QLatin1Char(':'), 1, 1);
        if (d == dim.dim)
            had.insert(t);
    }
    for (const QString &t : wanted)
        if (!had.contains(t))
            emit facetToggled(rec_.path, dim.dim, t, true);
    for (const QString &t : had)
        if (!wanted.contains(t))
            emit facetToggled(rec_.path, dim.dim, t, false);
}

void DetailPanel::openUcsDialog()
{
    if (rec_.path.isEmpty())
        return;
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("选择 UCS 分类"));
    dlg.resize(360, 480);
    auto *lay = new QVBoxLayout(&dlg);
    auto *filter = new QLineEdit(&dlg);
    filter->setPlaceholderText(QStringLiteral("搜索（中文名或 CatID）…"));
    lay->addWidget(filter);
    auto *list = new QListWidget(&dlg);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    for (const UcsCat &u : ucsCats_) {
        auto *item = new QListWidgetItem(
            QStringLiteral("%1 · %2").arg(u.zh, u.id), list);
        item->setData(Qt::UserRole, u.id);
        item->setToolTip(u.id);
        if (u.id == rec_.ucsCat)
            item->setSelected(true);
    }
    connect(filter, &QLineEdit::textChanged, this, [list](const QString &t) {
        for (int i = 0; i < list->count(); ++i) {
            auto *item = list->item(i);
            const QString id = item->data(Qt::UserRole).toString();
            const QString txt = item->text();
            item->setHidden(!t.isEmpty()
                            && !txt.contains(t, Qt::CaseInsensitive)
                            && !id.contains(t, Qt::CaseInsensitive));
        }
    });
    lay->addWidget(list);
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(btns);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const auto sel = list->selectedItems();
    if (!sel.isEmpty())
        emit ucsChanged(rec_.path, sel.first()->data(Qt::UserRole).toString());
}

void DetailPanel::rebuildTagChips()
{
    if (!tagChipRow_ || !tagChipRow_->layout())
        return;
    QLayoutItem *child = nullptr;
    while ((child = tagChipRow_->layout()->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }
    for (const TagInfo &t : allTags_) {
        auto *chip = new QToolButton(tagChipRow_);
        chip->setObjectName(QStringLiteral("Pill"));
        chip->setText(t.name);
        chip->setCursor(Qt::PointingHandCursor);
        chip->setCheckable(true);
        chip->setChecked(rec_.tags.contains(t.name));
        chip->setToolTip(QStringLiteral("%1（%2 个素材）").arg(t.name).arg(t.count));
        // 选中态用标签自身颜色，未选中为淡色边框
        const QColor tc(t.color.isEmpty() ? QStringLiteral("#5B9DB8") : t.color);
        chip->setStyleSheet(
            QStringLiteral("QToolButton#Pill{background:transparent;border:1px solid %1;color:%1;}"
                           "QToolButton#Pill:checked{background:%1;color:#fff;border:1px solid %1;}"
                           "QToolButton#Pill:hover{background:%2;}")
                .arg(tc.name(), tc.name() + QStringLiteral("33")));
        // 用 rec_ 的当前状态判断（不捕获重建时的快照），避免连点错乱
        connect(chip, &QToolButton::clicked, this, [this, t] {
            if (rec_.path.isEmpty())
                return;
            if (rec_.tags.contains(t.name))
                emit tagRemoved(rec_.path, t.id);
            else
                emit tagAssigned(rec_.path, t.id);
        });
        tagChipRow_->layout()->addWidget(chip);
    }
    // 补全词库随标签列表更新
    if (tagModel_) {
        QStringList names;
        for (const TagInfo &t : allTags_)
            names << t.name;
        tagModel_->setStringList(names);
    }
    if (auto *l = qobject_cast<QHBoxLayout *>(tagChipRow_->layout()))
        l->addStretch(1);
}

void DetailPanel::rebuildStars()
{
    const auto stars = starRow_->findChildren<QToolButton *>();
    for (auto *b : stars) {
        const int i = b->property("star").toInt();
        b->setIcon(Icons::make(Icons::Path::Star,
            i <= rec_.rating ? QColor(0xF5, 0xB8, 0x4C) : theme_.muted));
        b->setChecked(i <= rec_.rating);
    }
}

void DetailPanel::clear()
{
    flushPendingNotes();
    rec_ = FileRecord();
    nameLabel_->setText(QStringLiteral("未选择素材"));
    pathLabel_->clear();
    if (ucsLabel_) {
        ucsLabel_->setText(QStringLiteral("—"));
        ucsLabel_->setStyleSheet(QString());
    }
    if (kindCombo_) {
        kindCombo_->blockSignals(true);
        kindCombo_->setCurrentIndex(0);
        kindCombo_->blockSignals(false);
    }
    notesEdit_->blockSignals(true);
    notesEdit_->clear();
    notesEdit_->blockSignals(false);
    rebuildTagChips();
    rebuildFacetChips();
}

void DetailPanel::flushPendingNotes()
{
    if (notesDebounce_ && notesDebounce_->isActive()) {
        notesDebounce_->stop();
        if (!rec_.path.isEmpty())
            emit notesChanged(rec_.path, notesEdit_->toPlainText());
    }
}

void DetailPanel::hideEvent(QHideEvent *event)
{
    flushPendingNotes();
    QWidget::hideEvent(event);
}

void DetailPanel::setThemeColors(const ThemeColors &c)
{
    theme_ = c;
}
