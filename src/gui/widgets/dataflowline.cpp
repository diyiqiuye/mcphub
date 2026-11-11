#include "gui/widgets/dataflowline.h"

#include <QPainter>
#include <QPainterPath>

#include "gui/theme.h"

DataFlowLine::DataFlowLine(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
    m_timer.setInterval(16);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        m_offset -= 0.55;
        if (m_offset < -1000.0) m_offset = 0.0;
        update();
    });
}

void DataFlowLine::setEndpoints(const QPointF& a, const QPointF& b) {
    if (m_a == a && m_b == b) return;
    m_a = a;
    m_b = b;
    update();
}

void DataFlowLine::setBend(qreal bend) {
    m_bend = bend;
    update();
}

void DataFlowLine::setActive(bool on) {
    if (m_active == on) return;
    m_active = on;
    if (on) {
        m_timer.start();
    } else {
        m_timer.stop();
        m_offset = 0.0;
    }
    update();
}

void DataFlowLine::setDashOffset(qreal v) {
    m_offset = v;
    update();
}

QPainterPath DataFlowLine::path() const {
    QPainterPath p(m_a);
    const qreal dx = (m_b.x() - m_a.x()) * 0.5;
    const qreal bend = (m_b.x() - m_a.x()) * m_bend;
    p.cubicTo(QPointF(m_a.x() + dx, m_a.y() + bend), QPointF(m_b.x() - dx, m_b.y() + bend), m_b);
    return p;
}

void DataFlowLine::paintEvent(QPaintEvent*) {
    if (m_a.isNull() && m_b.isNull()) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QPainterPath pp = path();

    if (m_active) {

        QColor glow = theme::Accent;
        glow.setAlpha(38);
        QPen gp(glow);
        gp.setWidthF(3.6);
        gp.setCapStyle(Qt::RoundCap);
        p.setCompositionMode(QPainter::CompositionMode_Plus);
        p.setPen(gp);
        p.drawPath(pp);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }

    QPen pen(m_active ? theme::Accent : theme::Line);
    pen.setWidthF(m_active ? 1.4 : 1.0);
    pen.setCapStyle(Qt::FlatCap);
    pen.setDashPattern({4.0, 4.0});
    pen.setDashOffset(m_offset);
    p.setPen(pen);
    p.drawPath(pp);
}
