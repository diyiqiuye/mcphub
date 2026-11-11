#pragma once

#include <QColor>
#include <QFont>
#include <QString>

namespace theme {

inline const QColor Bg{0x09, 0x09, 0x0B};
inline const QColor BgRaised{0x0E, 0x0E, 0x12};
inline const QColor Line{0x33, 0x33, 0x33};
inline const QColor LineDim{0x1E, 0x1E, 0x22};
inline const QColor Accent{0x2D, 0xD4, 0xBF};
inline const QColor Warn{0xF5, 0x9E, 0x0B};
inline const QColor Error{0xEF, 0x44, 0x44};
inline const QColor Text{0xA1, 0xA1, 0xAA};
inline const QColor TextDim{0x71, 0x71, 0x7A};
inline const QColor TextHi{0xE4, 0xE4, 0xE7};

inline QFont mono(int px = 12, QFont::Weight weight = QFont::Normal) {
    QFont f(QStringLiteral("Consolas"));
    f.setStyleHint(QFont::Monospace);
    f.setFamilies({QStringLiteral("JetBrains Mono"), QStringLiteral("Fira Code"),
                   QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas")});
    f.setPixelSize(px);
    f.setWeight(weight);
    return f;
}

inline QString styleSheet() {
    return QStringLiteral(R"QSS(
* { outline: none; }
QWidget { background: #09090B; color: #A1A1AA; }
QToolTip {
    background: #0E0E12; color: #E4E4E7; border: 1px solid #333333;
    padding: 3px 6px; font-family: Consolas;
}
QSplitter::handle { background: #333333; }
QSplitter::handle:hover { background: #2DD4BF; }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical { height: 1px; }
QPlainTextEdit {
    background: #09090B; border: none; color: #A1A1AA;
    selection-background-color: #1F4A45; selection-color: #E4E4E7;
}
QScrollBar:vertical { background: #09090B; width: 8px; margin: 0; }
QScrollBar::handle:vertical { background: #26262B; min-height: 24px; }
QScrollBar::handle:vertical:hover { background: #2DD4BF; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QScrollBar:horizontal { background: #09090B; height: 8px; }
QScrollBar::handle:horizontal { background: #26262B; min-width: 24px; }
QScrollBar::handle:horizontal:hover { background: #2DD4BF; }
QMenu {
    background: #0E0E12; border: 1px solid #333333; padding: 3px;
    font-family: Consolas; font-size: 11px;
}
QMenu::item { padding: 5px 18px 5px 12px; color: #A1A1AA; }
QMenu::item:selected { background: #13201F; color: #2DD4BF; }
QMenu::item:disabled { color: #3F3F46; }
QMenu::separator { height: 1px; background: #26262B; margin: 3px 6px; }
QDialog { background: #09090B; }
QLineEdit {
    background: #0E0E12; border: 1px solid #333333; color: #A1A1AA;
    padding: 4px 6px; font-family: Consolas; font-size: 11px;
}
)QSS");
}

}
