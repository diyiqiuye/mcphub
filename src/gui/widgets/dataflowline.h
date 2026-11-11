#pragma once

#include <QPainterPath>
#include <QPointF>
#include <QPropertyAnimation>
#include <QTimer>
#include <QWidget>

class DataFlowLine : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal dashOffset READ dashOffset WRITE setDashOffset)

public:
    explicit DataFlowLine(QWidget* parent = nullptr);

    void setEndpoints(const QPointF& a, const QPointF& b);
    void setBend(qreal bend);
    void setActive(bool on);
    bool isActive() const { return m_active; }

    qreal dashOffset() const { return m_offset; }
    void setDashOffset(qreal v);

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    QPainterPath path() const;

    QPointF m_a;
    QPointF m_b;
    qreal m_bend = 0.18;
    qreal m_offset = 0.0;
    bool m_active = false;
    QTimer m_timer;
};
