#pragma once

#include <QByteArray>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

// A small USB stick, drawn from inline SVG so it needs no resource file. Shared by the Rekordbox
// and Serato drive rows. Must be called on the GUI thread (it makes a QPixmap).
inline QIcon usbDriveIcon() {
    static const QByteArray kSvg = QByteArrayLiteral(
            "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
            "<rect fill='#ffffff' x='7' y='1' width='10' height='8'/>"
            "<rect fill='#181818' x='9.2' y='3' width='1.8' height='3.2'/>"
            "<rect fill='#181818' x='13' y='3' width='1.8' height='3.2'/>"
            "<rect fill='#ffffff' x='5' y='9' width='14' height='14' rx='2.5'/>"
            "<rect fill='#181818' x='9' y='13' width='6' height='2' rx='1'/>"
            "</svg>");
    QSvgRenderer renderer(kSvg);
    QPixmap pixmap(96, 96);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    renderer.render(&painter);
    painter.end();
    return QIcon(pixmap);
}
