#pragma once

#include <QJsonObject>
#include <QPlainTextEdit>
#include <QVector>

class LogConsole : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit LogConsole(QWidget* parent = nullptr);

    void appendEntry(const QJsonObject& entry);
    void appendLine(const QString& level, const QString& source, const QString& text);

    void setPaused(bool paused);
    bool paused() const { return m_paused; }

private:
    bool atBottom() const;

    bool m_paused = false;
    QVector<QJsonObject> m_queue;
};
