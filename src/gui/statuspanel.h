#pragma once

#include <QJsonObject>
#include <QVector>
#include <QWidget>

#include "shared/protocol.h"

class QLabel;
class QVBoxLayout;
class NodeStatusIndicator;

class StatusPanel : public QWidget {
    Q_OBJECT
public:
    explicit StatusPanel(QWidget* parent = nullptr);

    void setOnline(bool online);
    void setSnapshot(const QJsonObject& snapshot);

signals:
    void toolMenuRequested(const QString& id, const QPoint& globalPos);

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    struct Row {
        NodeStatusIndicator* ring = nullptr;
        QLabel* name = nullptr;
        QLabel* state = nullptr;
        QLabel* pct = nullptr;
        QWidget* host = nullptr;
    };

    void rebuildRows(const QVector<mcphub::ToolStatus>& tools);
    static QString stateText(mcphub::ToolState s);
    static QString uptimeText(qint64 ms);

    QLabel* m_title = nullptr;
    QLabel* m_online = nullptr;
    QLabel* m_meta = nullptr;
    QLabel* m_footer = nullptr;
    QVBoxLayout* m_toolLayout = nullptr;
    QVector<Row> m_rows;
    QStringList m_order;
};
