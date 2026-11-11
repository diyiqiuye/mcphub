#include "gui/topologyview.h"

#include <QJsonArray>
#include <QFile>
#include <QLabel>
#include <QPainter>
#include <QResizeEvent>

#include "gui/theme.h"
#include "gui/widgets/dataflowline.h"
#include "gui/widgets/nodestatusindicator.h"

using namespace mcphub;

TopologyView::TopologyView(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(220);

    m_agentFlow = new DataFlowLine(this);
    m_agentFlow->setBend(0.0);

    m_gateway = new NodeStatusIndicator(this);
    m_gateway->setDiameter(76);
    m_gateway->setThickness(2.5);
    m_gateway->setState(NodeStatusIndicator::Idle);

    m_gatewayLabel = new QLabel(QStringLiteral("GATEWAY"), this);
    m_gatewayLabel->setFont(theme::mono(10, QFont::DemiBold));
    m_gatewayLabel->setStyleSheet(QStringLiteral("color:#A1A1AA; letter-spacing:1px;"));
    m_gatewayLabel->setAlignment(Qt::AlignCenter);

    m_gatewayState = new QLabel(QStringLiteral("stdio idle"), this);
    m_gatewayState->setFont(theme::mono(9));
    m_gatewayState->setStyleSheet(QStringLiteral("color:#52525B;"));
    m_gatewayState->setAlignment(Qt::AlignCenter);

    m_agent = new NodeStatusIndicator(this);
    m_agent->setDiameter(44);
    m_agent->setThickness(2.0);
    m_agent->setShowText(false);
    m_agent->setState(NodeStatusIndicator::Idle);

    m_agentLabel = new QLabel(QStringLiteral("AGENT"), this);
    m_agentLabel->setFont(theme::mono(10, QFont::DemiBold));
    m_agentLabel->setStyleSheet(QStringLiteral("color:#A1A1AA; letter-spacing:1px;"));
    m_agentLabel->setAlignment(Qt::AlignCenter);

    m_agentState = new QLabel(QStringLiteral("not configured"), this);
    m_agentState->setFont(theme::mono(9));
    m_agentState->setStyleSheet(QStringLiteral("color:#52525B;"));
    m_agentState->setAlignment(Qt::AlignCenter);
}

void TopologyView::rebuild(const QVector<ToolStatus>& tools) {
    if (m_nodes.size() == tools.size()) {
        bool same = true;
        for (int i = 0; i < tools.size(); ++i) {
            if (m_nodes.at(i).id != tools.at(i).id) { same = false; break; }
        }
        if (same) return;
    }
    for (auto& n : m_nodes) {
        delete n.ring;
        delete n.nameLabel;
        delete n.stateLabel;
        delete n.flow;
    }
    m_nodes.clear();

    for (const auto& t : tools) {
        Node n;
        n.id = t.id;
        n.name = t.name;
        n.flow = new DataFlowLine(this);
        n.flow->setBend(0.10);
        n.ring = new NodeStatusIndicator(this);
        n.ring->setDiameter(44);
        n.ring->setThickness(2.0);
        n.nameLabel = new QLabel(t.id.toUpper(), this);
        n.nameLabel->setFont(theme::mono(10, QFont::DemiBold));
        n.nameLabel->setStyleSheet(QStringLiteral("color:#A1A1AA;"));
        n.nameLabel->setAlignment(Qt::AlignCenter);
        n.stateLabel = new QLabel(QStringLiteral("-"), this);
        n.stateLabel->setFont(theme::mono(9));
        n.stateLabel->setStyleSheet(QStringLiteral("color:#52525B;"));
        n.stateLabel->setAlignment(Qt::AlignCenter);

        const QString tid = t.id;
        for (QWidget* w : {static_cast<QWidget*>(n.ring), static_cast<QWidget*>(n.nameLabel),
                           static_cast<QWidget*>(n.stateLabel)}) {
            w->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(w, &QWidget::customContextMenuRequested, this, [this, w, tid](const QPoint& pos) {
                emit toolMenuRequested(tid, w->mapToGlobal(pos));
            });
        }
        n.flow->show();
        n.ring->show();
        n.nameLabel->show();
        n.stateLabel->show();
        m_nodes.append(n);
    }
}

void TopologyView::setSnapshot(const QJsonObject& snapshot) {
    const auto arr = snapshot.value("tools").toArray();
    QVector<ToolStatus> tools;
    tools.reserve(arr.size());
    for (const auto& v : arr) tools.append(toolStatusFromJson(v.toObject()));
    rebuild(tools);

    const auto hub = snapshot.value("hub").toObject();
    m_ready = hub.value("ready").toInt();
    m_total = hub.value("total").toInt();

    for (int i = 0; i < tools.size() && i < m_nodes.size(); ++i) {
        const auto& t = tools.at(i);
        auto& n = m_nodes[i];
        n.state = t.state;
        n.progress = t.progress;
        n.ring->setProgress(t.progress);
        switch (t.state) {
        case ToolState::Ready:
        case ToolState::Running: n.ring->setState(NodeStatusIndicator::Active); break;
        case ToolState::Broken: n.ring->setState(NodeStatusIndicator::Error); break;
        case ToolState::Absent: n.ring->setState(NodeStatusIndicator::Idle); break;
        default: n.ring->setState(NodeStatusIndicator::Warn); break;
        }
        QString stText = QStringLiteral("idle");
        QString stColor = QStringLiteral("#52525B");
        switch (t.state) {
        case ToolState::Ready:
        case ToolState::Running:
            stText = t.external ? QStringLiteral("ready \u00b7 ext") : QStringLiteral("ready");
            stColor = QStringLiteral("#2DD4BF");
            break;
        case ToolState::Degraded: stText = QStringLiteral("verify failed"); stColor = QStringLiteral("#F59E0B"); break;
        case ToolState::Broken: stText = QStringLiteral("broken"); stColor = QStringLiteral("#EF4444"); break;
        case ToolState::Absent: stText = QStringLiteral("not installed"); break;
        default: stText = QStringLiteral("busy"); stColor = QStringLiteral("#F59E0B"); break;
        }
        n.stateLabel->setText(stText);
        n.stateLabel->setStyleSheet(QStringLiteral("color:%1;").arg(stColor));
        n.flow->setActive(t.ready() && m_online);
        n.nameLabel->setToolTip(t.path.isEmpty() ? t.message : t.path);
    }

    const double ratio = m_total > 0 ? static_cast<double>(m_ready) / m_total : 0.0;
    m_gateway->setProgress(ratio);
    m_gateway->setState(m_online ? NodeStatusIndicator::Active : NodeStatusIndicator::Idle);
    m_gatewayState->setText(QStringLiteral("stdio \u00b7 %1/%2 ready").arg(m_ready).arg(m_total));
    m_agentFlow->setActive(m_online && m_ready > 0);
    m_agent->setState(m_online ? NodeStatusIndicator::Warn : NodeStatusIndicator::Idle);
    m_agentState->setText(m_online ? QStringLiteral("awaiting agent") : QStringLiteral("offline"));

    layoutNodes();
    update();
}

void TopologyView::setOnline(bool online) {
    m_online = online;
    m_gateway->setState(online ? NodeStatusIndicator::Active : NodeStatusIndicator::Idle);
    m_agentFlow->setActive(online && m_ready > 0);
    for (auto& n : m_nodes) n.flow->setActive(online && (n.state == ToolState::Ready || n.state == ToolState::Running));
    update();
}

void TopologyView::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (qEnvironmentVariableIsSet("MCPHUB_UI_DEBUG")) {
        QFile f(QStringLiteral("C:/mcphub/ui-debug.log"));
        if (f.open(QIODevice::WriteOnly | QIODevice::Append)) {
            f.write(QStringLiteral("resize %1x%2\n").arg(e->size().width()).arg(e->size().height()).toUtf8());
        }
    }
    layoutNodes();
}

void TopologyView::layoutNodes() {
    const int W = width();
    const int H = height();
    if (W <= 0 || H <= 0) return;

    if (qEnvironmentVariableIsSet("MCPHUB_UI_DEBUG")) {
        QFile f(QStringLiteral("C:/mcphub/ui-debug.log"));
        if (f.open(QIODevice::WriteOnly | QIODevice::Append)) {
            const QPoint p0 = m_nodes.isEmpty() ? QPoint() : m_nodes.first().ring->pos();
            const bool vis = m_nodes.isEmpty() ? false : m_nodes.first().ring->isVisible();
            const bool visLabel = m_nodes.isEmpty() ? false : m_nodes.first().nameLabel->isVisible();
            f.write(QStringLiteral("layout %1x%2 nodes=%3 first=%4,%5 vis=%6 labelVis=%7\n")
                        .arg(W).arg(H).arg(m_nodes.size()).arg(p0.x()).arg(p0.y())
                        .arg(vis).arg(visLabel)
                        .toUtf8());
        }
    }

    const int headH = 38;
    const int footH = 46;
    const int toolX = 26;
    const int agentX = W - 26 - 44;
    const int gateX = qMax(toolX + 170, qMin(static_cast<int>(W * 0.60), agentX - 160));

    const int n = m_nodes.size();
    const int usable = qMax(40, H - headH - footH);
    const int step = n > 0 ? usable / qMax(1, n) : usable;

    for (int i = 0; i < n; ++i) {
        auto& node = m_nodes[i];
        const int cy = headH + step * i + step / 2;
        node.ring->move(toolX, cy - 22);
        node.nameLabel->setGeometry(toolX - 40, cy + 24, 124, 12);
        node.stateLabel->setGeometry(toolX - 40, cy + 36, 124, 11);
        node.flow->setGeometry(rect());
        node.flow->setEndpoints(QPointF(toolX + 44, cy), QPointF(gateX, H / 2));
    }

    m_gateway->move(gateX, H / 2 - 38);
    m_gatewayLabel->setGeometry(gateX - 40, H / 2 + 40, 156, 12);
    m_gatewayState->setGeometry(gateX - 40, H / 2 + 52, 156, 11);

    m_agent->move(agentX, H / 2 - 22);
    m_agentLabel->setGeometry(agentX - 52, H / 2 + 24, 148, 12);
    m_agentState->setGeometry(agentX - 52, H / 2 + 36, 148, 11);

    m_agentFlow->setGeometry(rect());
    m_agentFlow->setEndpoints(QPointF(gateX + 76, H / 2), QPointF(agentX, H / 2));

    for (auto& node : m_nodes) node.flow->lower();
    m_agentFlow->lower();
    for (auto& node : m_nodes) {
        node.ring->raise();
        node.nameLabel->raise();
        node.stateLabel->raise();
    }
    m_gateway->raise();
    m_gatewayLabel->raise();
    m_gatewayState->raise();
    m_agent->raise();
    m_agentLabel->raise();
    m_agentState->raise();
}

void TopologyView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), theme::Bg);
    p.setRenderHint(QPainter::Antialiasing, true);

    p.setFont(theme::mono(9, QFont::DemiBold));
    p.setPen(theme::TextDim);
    p.drawText(QRect(16, 12, 200, 14), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("TOOLS"));
    if (m_gateway) {
        const int cx = m_gateway->geometry().center().x();
        p.drawText(QRect(cx - 90, 12, 180, 14), Qt::AlignCenter, QStringLiteral("GATEWAY"));
    }
    if (m_agent) {
        const int cx = m_agent->geometry().center().x();
        p.drawText(QRect(cx - 90, 12, 180, 14), Qt::AlignCenter, QStringLiteral("AGENT"));
    }

    p.setPen(theme::Line);
    p.drawLine(0, 30, width(), 30);

    p.setFont(theme::mono(9));
    p.setPen(theme::TextDim);
    p.drawText(QRect(16, height() - 18, width() - 32, 14), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("\u25cf ready   \u25d0 busy   \u25cb idle   \u2715 broken"));

    p.setPen(theme::Line);
    p.drawLine(0, height() - 1, width(), height() - 1);
}
