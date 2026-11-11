#include "gui/logconsole.h"

#include <QDateTime>
#include <QJsonValue>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextCharFormat>

#include "gui/theme.h"

LogConsole::LogConsole(QWidget* parent) : QPlainTextEdit(parent) {
    setReadOnly(true);
    setUndoRedoEnabled(false);
    setMaximumBlockCount(2000);
    setFrameShape(QFrame::NoFrame);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    setFont(theme::mono(11));
    document()->setDocumentMargin(6);
}

bool LogConsole::atBottom() const {
    auto* sb = verticalScrollBar();
    return sb->value() >= sb->maximum() - 4;
}

void LogConsole::appendEntry(const QJsonObject& e) {
    if (m_paused) {
        if (m_queue.size() < 800) m_queue.append(e);
        return;
    }
    appendLine(e.value("level").toString(QStringLiteral("info")),
               e.value("source").toString(QStringLiteral("core")),
               e.value("text").toString());
}

void LogConsole::setPaused(bool paused) {
    m_paused = paused;
    if (!paused && !m_queue.isEmpty()) {
        const auto pending = m_queue;
        m_queue.clear();
        for (const auto& e : pending) appendEntry(e);
    }
}

void LogConsole::appendLine(const QString& level, const QString& source, const QString& text) {
    const bool follow = atBottom();

    QTextCursor cur(document());
    cur.movePosition(QTextCursor::End);

    QTextCharFormat tsFmt;
    tsFmt.setForeground(QColor(0x3F, 0x3F, 0x46));
    tsFmt.setFont(theme::mono(10));

    QTextCharFormat srcFmt;
    QColor srcColor = theme::Accent;
    srcColor.setAlpha(190);
    srcFmt.setForeground(srcColor);
    srcFmt.setFont(theme::mono(11));

    QTextCharFormat bodyFmt;
    QColor body = theme::Text;
    if (level == QStringLiteral("error")) body = theme::Error;
    else if (level == QStringLiteral("warn")) body = theme::Warn;
    else if (level == QStringLiteral("debug")) body = theme::TextDim;
    else if (level == QStringLiteral("ok")) body = theme::Accent;
    bodyFmt.setForeground(body);
    bodyFmt.setFont(theme::mono(11));

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    cur.insertText(stamp + QStringLiteral(" "), tsFmt);
    cur.insertText(source, srcFmt);
    cur.insertText(QStringLiteral("  "), bodyFmt);
    cur.insertText(text, bodyFmt);
    cur.insertBlock();

    if (follow) {
        auto* sb = verticalScrollBar();
        sb->setValue(sb->maximum());
    }
}
