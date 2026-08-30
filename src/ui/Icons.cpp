#include "ui/Icons.h"

#include <QSvgRenderer>
#include <QPainter>
#include <QPixmap>

namespace Icons {

QIcon make(const char *svgBody, const QColor &stroke, int size)
{
    const QString svg = QStringLiteral(
        "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' "
        "fill='none' stroke='%1' stroke-width='1.6' stroke-linecap='round' stroke-linejoin='round'>%2</svg>")
        .arg(stroke.name(), QString::fromLatin1(svgBody));

    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    QSvgRenderer r;
    r.load(svg.toUtf8());
    r.render(&p);
    p.end();
    return QIcon(pm);
}

} // namespace Icons
