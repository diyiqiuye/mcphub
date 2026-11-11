#include "gui/titlebar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QWindow>

#include "gui/theme.h"

TitleBar::TitleBar(QWidget* parent) : QWidget(parent) {
    setFixedHeight(28);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(10, 0, 4, 0);
    lay->setSpacing(8);

    m_brand = new QLabel(QStringLiteral("\u25c8 MCP HUB"), this);
    m_brand->setFont(theme::mono(12, QFont::DemiBold));
    m_brand->setStyleSheet(QStringLiteral("color:#2DD4BF; letter-spacing:1px;"));

    m_sub = new QLabel(QStringLiteral("/ mcphubd"), this);
    m_sub->setFont(theme::mono(11));
    m_sub->setStyleSheet(QStringLiteral("color:#52525B;"));

    m_status = new QLabel(QStringLiteral("\u25cf daemon offline"), this);
    m_status->setFont(theme::mono(11));
    m_status->setStyleSheet(QStringLiteral("color:#EF4444;"));

    m_summary = new QLabel(QString(), this);
    m_summary->setFont(theme::mono(11));
    m_summary->setStyleSheet(QStringLiteral("color:#71717A;"));

    auto makeButton = [this](const QString& text, const QString& hoverColor) {
        auto* b = new QToolButton(this);
        b->setText(text);
        b->setFixedSize(30, 24);
        b->setCursor(Qt::ArrowCursor);
        b->setStyleSheet(QStringLiteral(
                             "QToolButton{border:none;background:transparent;color:#71717A;font:13px Consolas;}"
                             "QToolButton:hover{color:%1;background:#131317;}")
                             .arg(hoverColor));
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    };

    m_min = makeButton(QStringLiteral("\u2500"), QStringLiteral("#2DD4BF"));
    m_max = makeButton(QStringLiteral("\u25a2"), QStringLiteral("#2DD4BF"));
    m_close = makeButton(QStringLiteral("\u2715"), QStringLiteral("#EF4444"));

    lay->addWidget(m_brand);
    lay->addWidget(m_sub);
    lay->addSpacing(10);
    lay->addWidget(m_status);
    lay->addWidget(m_summary);
    lay->addStretch(1);
    lay->addWidget(m_min);
    lay->addWidget(m_max);
    lay->addWidget(m_close);

    connect(m_min, &QToolButton::clicked, this, &TitleBar::minimizeRequested);
    connect(m_max, &QToolButton::clicked, this, &TitleBar::maximizeRequested);
    connect(m_close, &QToolButton::clicked, this, &TitleBar::closeRequested);
}

void TitleBar::setOnline(bool online) {
    m_status->setText(online ? QStringLiteral("\u25cf daemon online") : QStringLiteral("\u25cf daemon offline"));
    m_status->setStyleSheet(online ? QStringLiteral("color:#2DD4BF;") : QStringLiteral("color:#EF4444;"));
}

void TitleBar::setSummary(const QString& text) {
    m_summary->setText(text);
}

void TitleBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), theme::Bg);
    p.setPen(theme::Line);
    p.drawLine(0, height() - 1, width(), height() - 1);
}

void TitleBar::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        if (auto* w = window()->windowHandle()) w->startSystemMove();
    }
    QWidget::mousePressEvent(e);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) emit maximizeRequested();
    QWidget::mouseDoubleClickEvent(e);
}

void TitleBar::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
}
