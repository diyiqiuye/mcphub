#pragma once

#include <QJsonObject>
#include <QVector>
#include <QWidget>

#include "shared/protocol.h"

class QLabel;
class NodeStatusIndicator;
class DataFlowLine;

class TopologyView : public QWidget {
    Q_OBJECT
public:
    explicit TopologyView(QWidget* parent = nullptr);

    void setOnline(bool online);
    void setSnapshot(const QJsonObject& snapshot);

signals:
    void toolMenuRequested(const QString& id, const QPoint& globalPos);

protected:
    void resizeEvent(QResizeEvent* e) override;
    void paintEvent(QPaintEvent* e) override;

private:
    struct Node {
        QString id;
        QString name;
        NodeStatusIndicator* ring = nullptr;
        QLabel* nameLabel = nullptr;
        QLabel* stateLabel = nullptr;
        DataFlowLine* flow = nullptr;
        mcphub::ToolState state = mcphub::ToolState::Absent;
        double progress = 0.0;
    };

    void rebuild(const QVector<mcphub::ToolStatus>& tools);
    void layoutNodes();

    QVector<Node> m_nodes;
    NodeStatusIndicator* m_gateway = nullptr;
    QLabel* m_gatewayLabel = nullptr;
    QLabel* m_gatewayState = nullptr;
    NodeStatusIndicator* m_agent = nullptr;
    QLabel* m_agentLabel = nullptr;
    QLabel* m_agentState = nullptr;
    DataFlowLine* m_agentFlow = nullptr;

    bool m_online = false;
    int m_ready = 0;
    int m_total = 0;
};
