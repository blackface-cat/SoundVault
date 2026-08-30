#pragma once

#include <QIcon>
#include <QColor>

/**
 * 统一线性图标集（24x24 viewBox，stroke 1.6，圆头圆角）。
 * 全部为内联 SVG，由 QSvgRenderer 渲染，随主题色生成，无外部资源。
 * 图标风格契约：线性描边、1.6px、语义清晰、同族不混用。
 */
namespace Icons {

/// 由 SVG 路径体 + 主题色生成 16px 图标
QIcon make(const char *svgBody, const QColor &stroke, int size = 16);

namespace Path {
inline const char *Search   = "<circle cx='11' cy='11' r='7'/><line x1='16.5' y1='16.5' x2='21' y2='21'/>";
inline const char *List     = "<line x1='9' y1='6' x2='20' y2='6'/><line x1='9' y1='12' x2='20' y2='12'/><line x1='9' y1='18' x2='20' y2='18'/><line x1='4' y1='6' x2='4.01' y2='6'/><line x1='4' y1='12' x2='4.01' y2='12'/><line x1='4' y1='18' x2='4.01' y2='18'/>";
inline const char *Grid     = "<rect x='4' y='4' width='6' height='6'/><rect x='14' y='4' width='6' height='6'/><rect x='4' y='14' width='6' height='6'/><rect x='14' y='14' width='6' height='6'/>";
inline const char *Moon     = "<path d='M21 12.8A9 9 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8z'/>";
inline const char *Sun      = "<circle cx='12' cy='12' r='4'/><line x1='12' y1='2' x2='12' y2='5'/><line x1='12' y1='19' x2='12' y2='22'/><line x1='2' y1='12' x2='5' y2='12'/><line x1='19' y1='12' x2='22' y2='12'/><line x1='4.9' y1='4.9' x2='7' y2='7'/><line x1='17' y1='17' x2='19.1' y2='19.1'/><line x1='4.9' y1='19.1' x2='7' y2='17'/><line x1='17' y1='7' x2='19.1' y2='4.9'/>";
inline const char *Sliders  = "<line x1='4' y1='7' x2='20' y2='7'/><circle cx='9' cy='7' r='2'/><line x1='4' y1='17' x2='20' y2='17'/><circle cx='15' cy='17' r='2'/>";
inline const char *Folder   = "<path d='M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z'/>";
inline const char *Drive    = "<path d='M5 12l2-6h10l2 6'/><rect x='3' y='12' width='18' height='8' rx='2'/><line x1='7' y1='16' x2='7.01' y2='16'/><line x1='11' y1='16' x2='11.01' y2='16'/>";
inline const char *Star     = "<path d='M12 3l2.7 5.8 6.3.8-4.6 4.4 1.2 6.2-5.6-3.1-5.6 3.1 1.2-6.2L3 9.6l6.3-.8z'/>";
inline const char *Plus     = "<line x1='12' y1='5' x2='12' y2='19'/><line x1='5' y1='12' x2='19' y2='12'/>";
inline const char *Import   = "<path d='M14 3H6a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V9z'/><path d='M14 3v6h6'/><line x1='12' y1='12' x2='12' y2='18'/><line x1='9' y1='15' x2='15' y2='15'/>";
inline const char *Play     = "<path d='M8 5v14l11-7z'/>";
inline const char *Pause    = "<line x1='9' y1='5' x2='9' y2='19'/><line x1='15' y1='5' x2='15' y2='19'/>";
inline const char *Stop     = "<rect x='7' y='7' width='10' height='10'/>";
inline const char *Repeat   = "<path d='M17 2l4 4-4 4'/><path d='M3 11V9a4 4 0 0 1 4-4h14'/><path d='M7 22l-4-4 4-4'/><path d='M21 13v2a4 4 0 0 1-4 4H3'/>";
inline const char *Volume   = "<path d='M11 5L6 9H3v6h3l5 4z'/><path d='M15.5 8.5a5 5 0 0 1 0 7'/><path d='M18.5 5.5a9 9 0 0 1 0 13'/>";
inline const char *Audio    = "<path d='M9 18V6l10-2v12'/><circle cx='6.5' cy='18' r='2.5'/><circle cx='16.5' cy='16' r='2.5'/>";
inline const char *Wave     = "<path d='M3 12h2l2-7 3 14 3-11 2 4h4'/>";
inline const char *Spectrum = "<line x1='5' y1='18' x2='5' y2='8'/><line x1='10' y1='18' x2='10' y2='3'/><line x1='15' y1='18' x2='15' y2='11'/><line x1='20' y1='18' x2='20' y2='5'/>";
inline const char *ChevronUp= "<path d='M6 15l6-6 6 6'/>";
inline const char *ChevronDown = "<path d='M6 9l6 6 6-6'/>";
inline const char *Download = "<path d='M12 3v12'/><path d='M7 10l5 5 5-5'/><path d='M4 21h16'/>";
inline const char *Tag      = "<path d='M3 3h8l10 10-8 8L3 11z'/><circle cx='8' cy='8' r='1.5'/>";
inline const char *Scissors = "<circle cx='6' cy='6' r='3'/><circle cx='6' cy='18' r='3'/><line x1='8.1' y1='8.1' x2='20' y2='20'/><line x1='8.1' y1='15.9' x2='20' y2='4'/>";
inline const char *Home     = "<path d='M3 10.5L12 3l9 7.5'/><path d='M5 9.5V21h14V9.5'/><line x1='10' y1='21' x2='10' y2='14'/><line x1='14' y1='21' x2='14' y2='14'/>";
inline const char *Clock    = "<circle cx='12' cy='12' r='8.5'/><path d='M12 7v5l3.5 2'/>";
inline const char *Info     = "<circle cx='12' cy='12' r='9'/><line x1='12' y1='11' x2='12' y2='16'/><line x1='12' y1='7.5' x2='12.01' y2='7.5'/>";
inline const char *Prev     = "<path d='M19 20L9 12l10-8v16z'/>";
inline const char *Next     = "<path d='M5 4l10 8-10 8V4z'/>";
inline const char *Category = "<rect x='3' y='3' width='7' height='7' rx='1.5'/><rect x='14' y='3' width='7' height='7' rx='1.5'/><rect x='3' y='14' width='7' height='7' rx='1.5'/><rect x='14' y='14' width='7' height='7' rx='1.5'/>";
}

} // namespace Icons
