#pragma once

#include <QPoint>
#include <QString>

class QWidget;

/// A small message shown inside a window for a few seconds, never taking taps. Used where a
/// tooltip is too small to read on the Pi's touchscreen and a dialog can end up behind its
/// fullscreen kiosk window (a child widget of the main window can't).
namespace WNotice {
/// Shows `text` in `pWindow`. `anchor` is in the window's coordinates: the message's top-left
/// corner goes there, or its top-centre if `centered`. It is kept inside the window.
void show(QWidget* pWindow, const QString& text, const QPoint& anchor, bool centered = false);
} // namespace WNotice
