#include "gui/widgets/nodestatusindicator.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>

#include "gui/theme.h"

NodeStatusIndicator::NodeStatusIndicator(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_anim = new QPropertyAnimation(this, "animValue", this);
    m_anim->setDuration(320);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_anim, &QPropertyAnimation::finished, this, [this] { update(); });
}

void NodeStatusIndicator::setProgress(double p) {
    p = qBound(0.0, p, 1.0);
    if (qFuzzyCompare(p + 1.0, m_target + 1.0)) return;
    m_target = p;
    m_anim->stop();
    m_anim->setStartValue(m_progress);
    m_anim->setEndValue(p);
    m_anim->start();
}

void NodeStatusIndicator::setAnimValue(double v) {
    m_progress = v;
    update();
}

void NodeStatusIndicator::setState(State s) {
    if (m_state == s) return;
    m_state = s;
    update();
}

void NodeStatusIndicator::setShowText(bool on) {
    m_showText = on;
    update();
}

void NodeStatusIndicator::setThickness(qreal t) {
    m_thickness = qMax(1.0, t);
    update();
}

void NodeStatusIndicator::setDiameter(int d) {
    m_diameter = qMax(12, d);
    setFixedSize(m_diameter, m_diameter);
    update();
}

QSize NodeStatusIndicator::sizeHint() const {
    return {m_diameter, m_diameter};
}

QSize NodeStatusIndicator::minimumSizeHint() const {
    return {16, 16};
}

QColor NodeStatusIndicator::ringColor() const {
    switch (m_state) {
    case Active: return theme::Accent;
    case Warn: return theme::Warn;
    case Error: return theme::Error;
    case Idle:
    default: return theme::Line;
    }
}

void NodeStatusIndicator::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal side = qMin(width(), height());
    const qreal pad = qMin(m_thickness * 3.0 + 1.0, side * 0.18);
    QRectF box((width() - side) / 2.0 + pad, (height() - side) / 2.0 + pad, side - pad * 2, side - pad * 2);
    if (box.width() <= 1) return;

    QColor baseColor = theme::Line;
    if (m_progress < 0.01) {
        if (m_state == Warn) baseColor = QColor(0xF5, 0x9E, 0x0B, 130);
        else if (m_state == Error) baseColor = QColor(0xEF, 0x44, 0x44, 130);
    }
    QPen base(baseColor);
    base.setWidthF(m_thickness);
    p.setPen(base);
    p.drawEllipse(box);

    const double pr = qBound(0.0, m_progress, 1.0);
    if (pr > 0.001) {
        const QColor c = ringColor();
        const int span = -static_cast<int>(pr * 360.0 * 16.0);
        const int start = 90 * 16;

        QColor glow = c;
        glow.setAlpha(46);
        QPen glowPen(glow);
        glowPen.setWidthF(m_thickness * 3.2);
        glowPen.setCapStyle(Qt::RoundCap);
        p.setCompositionMode(QPainter::CompositionMode_Plus);
        p.setPen(glowPen);
        p.drawArc(box, start, span);

        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        QPen core(c);
        core.setWidthF(m_thickness);
        core.setCapStyle(Qt::RoundCap);
        p.setPen(core);
        p.drawArc(box, start, span);
    }

    if (m_showText && side >= 26) {
        const int tf = qMax(8, static_cast<int>(side * 0.27));
        p.setFont(theme::mono(tf));
        p.setPen(m_state == Idle ? theme::TextDim : theme::Text);
        p.drawText(rect(), Qt::AlignCenter, QString::number(qRound(pr * 100)));
    }
}
