#pragma once

#include <QColor>
#include <QString>

/**
 * 主题系统：明/暗双主题，语义 token。
 * v0.5 重新设计：亮色为默认，冷灰纸感底 + 单一蓝强调 + 语义状态色。
 */
struct ThemeColors {
    QColor bg;          // 窗口底
    QColor surface;     // 面板/卡片
    QColor surfaceAlt;  // 输入框/悬浮底
    QColor border;      // 分隔线
    QColor text;        // 主文字
    QColor muted;       // 次要文字
    QColor accent;      // 唯一强调色
    QColor accentSoft;  // 强调色淡底
    QColor selection;   // 选中行
    QColor ok;          // 语义：WAV/正常
    QColor warn;        // 语义：MP3/警告
    QColor danger;      // 语义：错误/离线
    QColor star;        // 收藏星标
    QColor waveformBg;  // 波形区底
    QColor waveformLine;
    QColor waveformPlayed;
};

namespace Theme {

ThemeColors colors(bool dark);
QString styleSheet(bool dark);
QString appVersion();

} // namespace Theme
