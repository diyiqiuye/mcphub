#pragma once

#include <QWidget>

class QLabel;
class QToolButton;

class TitleBar : public QWidget {
    Q_OBJECT
public:
    explicit TitleBar(QWidget* parent = nullptr);

    void setOnline(bool online);
    void setSummary(const QString& text);

signals:
    void minimizeRequested();
    void maximizeRequested();
    void closeRequested();

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    QLabel* m_brand = nullptr;
    QLabel* m_sub = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_summary = nullptr;
    QToolButton* m_min = nullptr;
    QToolButton* m_max = nullptr;
    QToolButton* m_close = nullptr;
};
