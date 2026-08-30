#include "ui/Theme.h"

namespace Theme {

QString appVersion() { return QStringLiteral("1.4.0"); }

ThemeColors colors(bool dark)
{
    ThemeColors c;
    if (dark) {
        // 暗色：专业深空紫调（参考 MATRIX WAVE），紫色唯一强调
        c.bg          = QColor(0x0F, 0x0F, 0x12);
        c.surface     = QColor(0x16, 0x16, 0x1A);
        c.surfaceAlt  = QColor(0x1E, 0x1E, 0x24);
        c.border      = QColor(0x2E, 0x2E, 0x38);
        c.text        = QColor(0xE7, 0xE7, 0xEC);
        c.muted       = QColor(0x8E, 0x8E, 0x9A);
        c.accent      = QColor(0x8B, 0x5C, 0xF6);
        c.accentSoft  = QColor(0x2A, 0x24, 0x40);
        c.selection   = QColor(0x2A, 0x24, 0x40);
        c.ok          = QColor(0x45, 0xC4, 0x8A);
        c.warn        = QColor(0xEE, 0xA6, 0x48);
        c.danger      = QColor(0xF2, 0x6D, 0x6D);
        c.star        = QColor(0xF5, 0xB8, 0x4C);
        c.waveformBg  = QColor(0x11, 0x11, 0x16);
        c.waveformLine = QColor(0x5B, 0x5B, 0x6B);
        c.waveformPlayed = QColor(0x8B, 0x5C, 0xF6);
    } else {
        // 亮色：纸感底 + 紫色强调（品牌一致）
        c.bg          = QColor(0xF2, 0xF0, 0xF6);
        c.surface     = QColor(0xFF, 0xFF, 0xFF);
        c.surfaceAlt  = QColor(0xF5, 0xF3, 0xFA);
        c.border      = QColor(0xDF, 0xDB, 0xE8);
        c.text        = QColor(0x23, 0x20, 0x2E);
        c.muted       = QColor(0x6E, 0x69, 0x7C);
        c.accent      = QColor(0x6D, 0x28, 0xD9);
        c.accentSoft  = QColor(0xEA, 0xE2, 0xFB);
        c.selection   = QColor(0xE4, 0xDA, 0xFA);
        c.ok          = QColor(0x11, 0x99, 0x6B);
        c.warn        = QColor(0xD9, 0x7A, 0x0F);
        c.danger      = QColor(0xD4, 0x3A, 0x3A);
        c.star        = QColor(0xE8, 0x9C, 0x1E);
        c.waveformBg  = QColor(0xF6, 0xF4, 0xFB);
        c.waveformLine = QColor(0xBE, 0xB6, 0xD2);
        c.waveformPlayed = QColor(0x6D, 0x28, 0xD9);
    }
    return c;
}

QString styleSheet(bool dark)
{
    const ThemeColors c = colors(dark);
    const QString bg         = c.bg.name();
    const QString surface    = c.surface.name();
    const QString surfaceAlt = c.surfaceAlt.name();
    const QString border     = c.border.name();
    const QString text       = c.text.name();
    const QString muted      = c.muted.name();
    const QString accent     = c.accent.name();
    const QString accentSoft = c.accentSoft.name();
    const QString selection  = c.selection.name();

    // 强调色交互层次：hover / pressed（亮色加深、暗色提亮）
    const QString accentHover = dark ? QColor(c.accent).lighter(110).name()
                                     : QColor(c.accent).darker(110).name();
    const QString accentPress = dark ? QColor(c.accent).lighter(120).name()
                                     : QColor(c.accent).darker(120).name();
    // 主按钮渐变顶部（比 accent 略亮）
    const QString accentHi    = dark ? QColor(c.accent).lighter(114).name()
                                     : QColor(c.accent).lighter(112).name();

    return QString(R"QSS(
* { outline: none; }
QMainWindow, QDialog { background: %1; }
QWidget {
    color: %4;
    font-family: "Microsoft YaHei UI", "PingFang SC", "Segoe UI", sans-serif;
    font-size: 13px;
}
QToolTip {
    background: %2; color: %4; border: 1px solid %3; border-radius: 6px;
    padding: 5px 9px; font-size: 12px;
}

/* ---- 顶栏 ---- */
#TopBar { background: %2; border-bottom: 1px solid %3; padding-left: 8px; padding-top: 3px; padding-bottom: 3px; spacing: 6px; }
#AppTitle { font-size: 16px; font-weight: 800; letter-spacing: 1px; color: %4; }
#AppSub { color: %5; font-size: 11px; padding-top: 4px; }
#StatsLabel { color: %5; font-size: 12px; padding: 0 6px; }

/* ---- 输入框 ---- */
#SearchField {
    background: %6; border: 1px solid %3; border-radius: 9px;
    padding: 5px 11px; selection-background-color: %8; color: %4;
}
#SearchField:hover { border-color: %5; }
#SearchField:focus { border: 1px solid %7; background: %2; }
#SearchField::placeholder { color: %5; }
QLineEdit, QTextEdit { selection-background-color: %8; }

/* ---- 图标按钮 ---- */
QToolButton#IconBtn {
    background: transparent; border: none; border-radius: 8px; padding: 6px;
}
QToolButton#IconBtn:hover { background: %6; }
QToolButton#IconBtn:pressed { background: %8; }
QToolButton#IconBtn:checked { background: %8; }
QToolButton#IconBtn:disabled { background: transparent; }

/* ---- 播放主按钮（圆形强调） ---- */
QToolButton#PlayBtn {
    background: %8; border: none; border-radius: 15px; padding: 6px;
}
QToolButton#PlayBtn:hover { background: %9; }
QToolButton#PlayBtn:pressed { background: %10; }
QToolButton#PlayBtn:disabled { background: %6; }

/* ---- 胶囊按钮 ---- */
QPushButton#Pill, QToolButton#Pill {
    background: transparent; border: 1px solid transparent; border-radius: 999px;
    padding: 5px 13px; text-align: left; font-size: 12.5px; color: %4;
}
QPushButton#Pill:hover, QToolButton#Pill:hover { background: %6; border: 1px solid %3; }
QPushButton#Pill:pressed, QToolButton#Pill:pressed { background: %8; }
QPushButton#Pill:checked, QToolButton#Pill:checked {
    background: %8; color: %7; font-weight: 600; border: 1px solid %7;
}

/* ---- 主操作按钮（渐变） ---- */
QPushButton#PrimaryBtn {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %11, stop:1 %7);
    color: #FFFFFF; border: none; border-radius: 9px;
    padding: 7px 18px; font-weight: 600; text-align: center;
}
QPushButton#PrimaryBtn:hover { background: %9; }
QPushButton#PrimaryBtn:pressed { background: %10; }
QPushButton#PrimaryBtn:disabled { background: %5; color: %2; }

/* 兼容旧对象名 */
QPushButton#ImportBtn {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %11, stop:1 %7);
    color: #FFFFFF; border: none; border-radius: 9px;
    padding: 8px 14px; font-weight: 600; text-align: center;
}
QPushButton#ImportBtn:hover { background: %9; }
QPushButton#ImportBtn:pressed { background: %10; }
QPushButton#ImportBtn:disabled { background: %5; color: %2; }

/* ---- 侧栏 / 树 / 列表 ---- */
#SideBar { background: %6; border-right: 1px solid %3; }
#DetailPane { background: %2; border-left: 2px solid %3; }
/* ---- 图标导轨（v0.8） ---- */
#NavRail { background: %2; border-right: 1px solid %3; }
QToolButton#NavBtn {
    background: transparent; border: none; border-radius: 8px;
    padding: 10px 0; margin: 2px 6px;
}
QToolButton#NavBtn:hover { background: %6; }
QToolButton#NavBtn:checked { background: %8; }
QToolButton#NavBtn:disabled { background: transparent; }
#NavSep { background: %3; border: none; min-height: 1px; max-height: 1px; margin: 6px 10px; }
#AppLogo { border: none; }
QFrame#SideCard {
    background: %2; border: 1px solid %3; border-radius: 10px;
}
QFrame#HLine { background: %3; border: none; min-height: 1px; max-height: 1px; }
#SideSection {
    color: %5; font-size: 11px; font-weight: 700; letter-spacing: 1.5px;
    padding: 0 0 0 7px; border-left: 3px solid %7;
}
QTreeView#SideTree { background: transparent; border: none; outline: none; }
QTreeView#SideTree::item { padding: 5px 6px; border-radius: 7px; color: %4; }
QTreeView#SideTree::item:hover { background: %6; }
QTreeView#SideTree::item:selected { background: %8; color: %4; }
QListWidget#SideTree { background: transparent; border: none; outline: none; }
QListWidget#SideTree::item { padding: 6px 8px; border-radius: 7px; color: %4; }
QListWidget#SideTree::item:hover { background: %6; }
QListWidget#SideTree::item:selected { background: %8; color: %4; }
QListWidget, QTreeView { background: transparent; }

/* ---- 文件区 ---- */
#FileArea { background: %1; }
#FilterRow { background: %2; border-bottom: 1px solid %3; }
#ListHeader { color: %5; font-size: 11.5px; }
#DropZone {
    border: 1.5px dashed %7; border-radius: 14px; color: %5; font-size: 13px;
    background: %8;
}
QTableView#FileTable {
    background: %2; alternate-background-color: %6; border: none;
    gridline-color: transparent; selection-background-color: %8; selection-color: %4;
}
QTableView#FileTable::item { padding: 4px 8px; border-bottom: 1px solid %3; }
QHeaderView::section {
    background: %6; color: %5; border: none; border-right: 1px solid %3;
    border-bottom: 1px solid %3;
    padding: 7px 8px; font-size: 12px; font-weight: 600;
}
QTableView QTableCornerButton::section { background: %6; border: none; }

/* ---- 播放条（底部） ---- */
#PlayerBar { background: %6; border-top: 1px solid %3; }
#TimeLabel { color: %5; font-size: 12px; font-variant-numeric: tabular-nums; }
#VolLabel { color: %5; font-size: 12px; }
#NowPlaying { color: %4; font-size: 12.5px; font-weight: 600; }
QSlider { min-height: 22px; }
QSlider::groove:horizontal { height: 4px; background: %6; border-radius: 2px; }
QSlider::sub-page:horizontal { background: %7; border-radius: 2px; }
QSlider::handle:horizontal {
    width: 12px; height: 12px; margin: -5px 0; border-radius: 6px;
    background: %2; border: 2px solid %7;
}
QSlider::handle:horizontal:hover { border-color: %9; background: %9; }
QSlider::handle:horizontal:disabled { background: %5; border-color: %3; }

/* ---- 滚动条 ---- */
QScrollBar:vertical { background: transparent; width: 9px; margin: 2px; }
QScrollBar::handle:vertical { background: %3; border-radius: 4px; min-height: 28px; }
QScrollBar::handle:vertical:hover { background: %5; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QScrollBar:horizontal { background: transparent; height: 9px; margin: 2px; }
QScrollBar::handle:horizontal { background: %3; border-radius: 4px; min-width: 28px; }
QScrollBar::handle:horizontal:hover { background: %5; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }

/* ---- 下拉框 / 单选 / 复选 ---- */
QComboBox {
    background: %6; border: 1px solid %3; border-radius: 8px; padding: 4px 26px 4px 10px; color: %4;
}
QComboBox:hover { border-color: %5; }
QComboBox:focus { border-color: %7; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox::down-arrow {
    image: none; border-left: 4px solid transparent; border-right: 4px solid transparent;
    border-top: 5px solid %5; margin-right: 8px;
}
QComboBox QAbstractItemView {
    background: %2; border: 1px solid %3; border-radius: 8px;
    selection-background-color: %8; selection-color: %4; padding: 4px; outline: none;
}
QRadioButton, QCheckBox { spacing: 7px; color: %4; }
QRadioButton::indicator, QCheckBox::indicator { width: 15px; height: 15px; }
QRadioButton::indicator { border: 1.5px solid %5; border-radius: 8px; background: %2; }
QRadioButton::indicator:checked { border: 4.5px solid %7; background: %2; }
QCheckBox::indicator { border: 1.5px solid %5; border-radius: 4px; background: %2; }
QCheckBox::indicator:checked { background: %7; border-color: %7;
    image: none; }

/* ---- 状态栏 / 菜单 / 分割条 ---- */
QStatusBar { background: %2; border-top: 1px solid %3; color: %5; font-size: 12px; }
QStatusBar::item { border: none; }
QMenuBar { background: %2; border-bottom: 1px solid %3; }
QMenuBar::item { padding: 5px 11px; background: transparent; border-radius: 6px; margin: 2px; }
QMenuBar::item:selected { background: %6; }
QMenu { background: %2; border: 1px solid %3; border-radius: 9px; padding: 6px; }
QMenu::item { padding: 6px 26px 6px 14px; border-radius: 7px; }
QMenu::item:selected { background: %8; }
QMenu::separator { height: 1px; background: %3; margin: 5px 8px; }
QSplitter::handle { background: transparent; }
QSplitter::handle:horizontal { width: 4px; }
QSplitter::handle:vertical { height: 4px; }
QSplitter::handle:hover { background: %8; }
QSplitter::handle:pressed { background: %7; }
QProgressBar {
    background: %6; border: none; border-radius: 5px; height: 10px; text-align: center;
    color: transparent; font-size: 8px;
}
QProgressBar::chunk { background: %7; border-radius: 5px; }
)QSS").arg(bg, surface, border, text, muted, surfaceAlt, accent, selection,
           accentHover, accentPress, accentHi);
}

} // namespace Theme
