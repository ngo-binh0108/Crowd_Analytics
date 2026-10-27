#pragma once

#include "core/Types.h"

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

class QApplication;
class QWidget;

namespace Theme {

// Surfaces & text
inline QColor bg()        { return QColor(0x0a, 0x12, 0x22); }
inline QColor sidebar()   { return QColor(0x0c, 0x16, 0x28); }
inline QColor surface()   { return QColor(0x10, 0x1b, 0x2e); }
inline QColor surface2()  { return QColor(0x14, 0x22, 0x3a); }
inline QColor surface3()  { return QColor(0x1a, 0x2b, 0x48); }
inline QColor border()    { return QColor(0x1d, 0x2c, 0x47); }
inline QColor border2()   { return QColor(0x2a, 0x3e, 0x62); }
inline QColor accent()    { return QColor(0x2f, 0x7d, 0xf6); }
inline QColor accent2()   { return QColor(0x5b, 0x9b, 0xff); }
inline QColor text()      { return QColor(0xe6, 0xed, 0xf7); }
inline QColor text2()     { return QColor(0x9d, 0xb0, 0xcc); }
inline QColor text3()     { return QColor(0x63, 0x75, 0x93); }
inline QColor videoBg()   { return QColor(0x05, 0x0a, 0x14); }

// Status palette (fixed; always paired with an icon or a label)
inline QColor good()      { return QColor(0x0c, 0xa3, 0x0c); }
inline QColor warning()   { return QColor(0xfa, 0xb2, 0x19); }
inline QColor serious()   { return QColor(0xec, 0x83, 0x5a); }
inline QColor critical()  { return QColor(0xd0, 0x3b, 0x3b); }

// Data series (categorical order, dark-surface steps)
inline QColor seriesIn()     { return QColor(0x39, 0x87, 0xe5); }
inline QColor seriesOut()    { return QColor(0xd9, 0x59, 0x26); }
inline QColor seriesPeople() { return QColor(0x19, 0x9e, 0x70); }
QColor categorical(int index);

QColor severityColor(Severity s);
QColor levelColor(DensityLevel l);
QColor statusColor(CameraStatus s);

QPixmap pixmap(const QString& name, const QColor& color, int size = 20);
QIcon icon(const QString& name, const QColor& color = text2(), const QColor& activeColor = QColor());
QIcon gridIcon(int cols, int rows, const QColor& color = text2(), const QColor& activeColor = QColor());

void apply(QApplication& app);

// Helpers for property-based styling
void setRole(QWidget* w, const char* role);
void repolish(QWidget* w);

} // namespace Theme
