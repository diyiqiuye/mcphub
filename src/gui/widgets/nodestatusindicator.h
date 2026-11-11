#pragma once

#include <QPropertyAnimation>
#include <QWidget>

class NodeStatusIndicator : public QWidget {
    Q_OBJECT
    Q_PROPERTY(double animValue READ progress WRITE setAnimValue)

public:
    enum State { Idle, Active, Warn, Error };
    Q_ENUM(State)

    explicit NodeStatusIndicator(QWidget* parent = nullptr);

    double progress() const { return m_progress; }
    void setProgress(double p);
    void setAnimValue(double v);

    State state() const { return m_state; }
    void setState(State s);

    void setShowText(bool on);
    void setThickness(qreal t);
    void setDiameter(int d);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    QColor ringColor() const;

    double m_progress = 0.0;
    double m_target = 0.0;
    State m_state = Idle;
    QPropertyAnimation* m_anim = nullptr;
    bool m_showText = true;
    qreal m_thickness = 2.5;
    int m_diameter = 48;
};
