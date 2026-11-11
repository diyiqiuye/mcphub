#include "gui/statuspanel.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

#include "gui/theme.h"
#include "gui/widgets/nodestatusindicator.h"

using namespace mcphub;

StatusPanel::StatusPanel(QWidget* parent) : QWidget(parent) {
    setMinimumWidth(240);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(2, 2, 2, 2);
    root->setSpacing(4);

    auto* head = new QHBoxLayout();
    head->setContentsMargins(4, 2, 4, 0);
    head->setSpacing(6);
    m_title = new QLabel(QStringLiteral("SYSTEM STATUS"), this);
    m_title->setFont(theme::mono(11, QFont::DemiBold));
    m_title->setStyleSheet(QStringLiteral("color:#71717A; letter-spacing:1.5px;"));
    m_online = new QLabel(QStringLiteral("\u25cf offline"), this);
    m_online->setFont(theme::mono(10));
    m_online->setStyleSheet(QStringLiteral("color:#EF4444;"));
    head->addWidget(m_title);
    head->addStretch(1);
    head->addWidget(m_online);
    root->addLayout(head);

    auto* gridHost = new QWidget(this);
    auto* grid = new QGridLayout(gridHost);
    grid->setContentsMargins(4, 2, 4, 2);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(1);
    auto addMeta = [&](int row, const QString& key) {
        auto* k = new QLabel(key, gridHost);
        k->setFont(theme::mono(10));
        k->setStyleSheet(QStringLiteral("color:#52525B;"));
        auto* v = new QLabel(QStringLiteral("-"), gridHost);
        v->setFont(theme::mono(11));
        v->setStyleSheet(QStringLiteral("color:#A1A1AA;"));
        grid->addWidget(k, row, 0);
        grid->addWidget(v, row, 1);
        return v;
    };
    m_meta = nullptr;
    auto* vUptime = addMeta(0, QStringLiteral("uptime"));
    auto* vClients = addMeta(1, QStringLiteral("clients"));
    auto* vReady = addMeta(2, QStringLiteral("ready"));
    auto* vCatalog = addMeta(3, QStringLiteral("catalog"));
    vUptime->setObjectName(QStringLiteral("v_uptime"));
    vClients->setObjectName(QStringLiteral("v_clients"));
    vReady->setObjectName(QStringLiteral("v_ready"));
    vCatalog->setObjectName(QStringLiteral("v_catalog"));
    grid->setColumnStretch(1, 1);
    root->addWidget(gridHost);

    auto* sep = new QWidget(this);
    sep->setFixedHeight(1);
    sep->setStyleSheet(QStringLiteral("background:#333333;"));
    root->addWidget(sep);

    m_toolLayout = new QVBoxLayout();
    m_toolLayout->setContentsMargins(0, 2, 0, 0);
    m_toolLayout->setSpacing(2);
    root->addLayout(m_toolLayout);

    root->addStretch(1);

    m_footer = new QLabel(QStringLiteral("pipe -"), this);
    m_footer->setFont(theme::mono(9));
    m_footer->setStyleSheet(QStringLiteral("color:#3F3F46;"));
    m_footer->setContentsMargins(4, 0, 4, 2);
    root->addWidget(m_footer);
}

void StatusPanel::setOnline(bool online) {
    m_online->setText(online ? QStringLiteral("\u25cf online") : QStringLiteral("\u25cf offline"));
    m_online->setStyleSheet(online ? QStringLiteral("color:#2DD4BF;") : QStringLiteral("color:#EF4444;"));
}

QString StatusPanel::stateText(ToolState s) {
    switch (s) {
    case ToolState::Absent: return QStringLiteral("IDLE");
    case ToolState::Planning: return QStringLiteral("PLAN");
    case ToolState::Downloading: return QStringLiteral("DOWN");
    case ToolState::Installing: return QStringLiteral("INST");
    case ToolState::Verifying: return QStringLiteral("VERI");
    case ToolState::Ready: return QStringLiteral("READY");
    case ToolState::Running: return QStringLiteral("RUN");
    case ToolState::Degraded: return QStringLiteral("DEGR");
    case ToolState::Broken: return QStringLiteral("FAIL");
    }
    return QStringLiteral("?");
}

QString StatusPanel::uptimeText(qint64 ms) {
    const qint64 s = ms / 1000;
    return QStringLiteral("%1:%2:%3")
        .arg(s / 3600, 2, 10, QLatin1Char('0'))
        .arg((s % 3600) / 60, 2, 10, QLatin1Char('0'))
        .arg(s % 60, 2, 10, QLatin1Char('0'));
}

void StatusPanel::rebuildRows(const QVector<ToolStatus>& tools) {
    QStringList ids;
    for (const auto& t : tools) ids << t.id;
    if (ids == m_order && !m_rows.isEmpty()) return;

    for (auto& r : m_rows) {
        if (r.host) {
            m_toolLayout->removeWidget(r.host);
            delete r.host;
        }
    }
    m_rows.clear();
    m_order = ids;

    for (const auto& t : tools) {
        auto* host = new QWidget(this);
        auto* lay = new QHBoxLayout(host);
        lay->setContentsMargins(4, 1, 4, 1);
        lay->setSpacing(6);

        Row r;
        r.host = host;
        r.ring = new NodeStatusIndicator(host);
        r.ring->setDiameter(16);
        r.ring->setThickness(2.0);
        r.ring->setShowText(false);

        r.name = new QLabel(t.id.toUpper(), host);
        r.name->setFont(theme::mono(11, QFont::DemiBold));
        r.name->setStyleSheet(QStringLiteral("color:#D4D4D8;"));

        r.state = new QLabel(QStringLiteral("IDLE"), host);
        r.state->setFont(theme::mono(10));
        r.state->setStyleSheet(QStringLiteral("color:#52525B;"));

        r.pct = new QLabel(QStringLiteral("  0"), host);
        r.pct->setFont(theme::mono(10));
        r.pct->setStyleSheet(QStringLiteral("color:#3F3F46;"));
        r.pct->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        r.pct->setFixedWidth(30);

        lay->addWidget(r.ring);
        lay->addWidget(r.name, 1);
        lay->addWidget(r.state);
        lay->addWidget(r.pct);

        host->setProperty("toolId", t.id);
        host->setContextMenuPolicy(Qt::CustomContextMenu);
        const QString tid = t.id;
        connect(host, &QWidget::customContextMenuRequested, this,
                [this, host, tid](const QPoint& pos) { emit toolMenuRequested(tid, host->mapToGlobal(pos)); });

        m_toolLayout->addWidget(host);
        m_rows.append(r);
    }
}

void StatusPanel::setSnapshot(const QJsonObject& snapshot) {
    const auto arr = snapshot.value("tools").toArray();
    QVector<ToolStatus> tools;
    tools.reserve(arr.size());
    for (const auto& v : arr) tools.append(toolStatusFromJson(v.toObject()));

    rebuildRows(tools);

    for (int i = 0; i < tools.size() && i < m_rows.size(); ++i) {
        const auto& t = tools.at(i);
        auto& r = m_rows[i];
        r.ring->setProgress(t.progress);
        switch (t.state) {
        case ToolState::Ready:
        case ToolState::Running: r.ring->setState(NodeStatusIndicator::Active); break;
        case ToolState::Broken: r.ring->setState(NodeStatusIndicator::Error); break;
        case ToolState::Absent: r.ring->setState(NodeStatusIndicator::Idle); break;
        default: r.ring->setState(NodeStatusIndicator::Warn); break;
        }
        r.state->setText(stateText(t.state));
        r.state->setStyleSheet(QStringLiteral("color:%1;").arg(
            t.state == ToolState::Ready || t.state == ToolState::Running ? QStringLiteral("#2DD4BF")
            : t.state == ToolState::Broken                              ? QStringLiteral("#EF4444")
            : t.state == ToolState::Absent                              ? QStringLiteral("#52525B")
                                                                        : QStringLiteral("#F59E0B")));
        r.pct->setText(QStringLiteral("%1").arg(static_cast<int>(t.progress * 100), 3));
        r.name->setStyleSheet(t.state == ToolState::Absent ? QStringLiteral("color:#71717A;")
                                                           : QStringLiteral("color:#D4D4D8;"));
        QString tip = QStringLiteral("%1\n%2\n%3")
                          .arg(t.name, stateKey(t.state), t.path.isEmpty() ? t.message : t.path);
        if (t.verified) {
            tip += QStringLiteral("\nverify: %1\n%2")
                       .arg(t.verifyOk ? QStringLiteral("ok") : QStringLiteral("failed"),
                            t.verifyText.left(200));
        }
        r.host->setToolTip(tip);
    }

    const auto hub = snapshot.value("hub").toObject();
    if (auto* v = findChild<QLabel*>(QStringLiteral("v_uptime")))
        v->setText(uptimeText(static_cast<qint64>(hub.value("uptimeMs").toDouble())));
    if (auto* v = findChild<QLabel*>(QStringLiteral("v_clients")))
        v->setText(QString::number(hub.value("clients").toInt()));
    if (auto* v = findChild<QLabel*>(QStringLiteral("v_ready")))
        v->setText(QStringLiteral("%1/%2").arg(hub.value("ready").toInt()).arg(hub.value("total").toInt()));
    if (auto* v = findChild<QLabel*>(QStringLiteral("v_catalog")))
        v->setText(QStringLiteral("%1 entries").arg(hub.value("catalogCount").toInt()));

    m_footer->setText(QStringLiteral("pipe %1").arg(hub.value("pipe").toString(QStringLiteral("-"))));
    const QString err = hub.value("catalogError").toString();
    m_footer->setToolTip(err.isEmpty() ? hub.value("root").toString() : err);
}

void StatusPanel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), theme::Bg);
    p.setPen(theme::Line);
    p.drawLine(width() - 1, 0, width() - 1, height());
}
