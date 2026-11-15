#include "ChatView.h"

#include "core/I18n.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollBar>
#include <QTimer>

namespace hr {

ChatView::ChatView(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(8);

    m_scroll = new QScrollArea;
    m_scroll->setWidgetResizable(true);
    m_scroll->setMinimumHeight(230);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_content = new QWidget;
    m_content->setLayoutDirection(Qt::LeftToRight);        // my bubbles on the right, the assistant's on the left, in both languages
    m_list = new QVBoxLayout(m_content);
    m_list->setContentsMargins(4, 4, 4, 4);
    m_list->setSpacing(8);
    m_list->addStretch(1);
    m_scroll->setWidget(m_content);
    lay->addWidget(m_scroll, 1);
    // A new bubble makes the content taller only after the layout ran, so follow the range of the scroll bar:
    // the newest message stays in view while the user is at the end (scrolling up to read stops it).
    QScrollBar* bar = m_scroll->verticalScrollBar();
    connect(bar, &QScrollBar::rangeChanged, this, [this, bar](int, int max) {
        if (m_follow) bar->setValue(max);
    });
    connect(bar, &QScrollBar::valueChanged, this, [this, bar](int v) { m_follow = v >= bar->maximum() - 4; });

    m_chips = new QWidget;
    lay->addWidget(m_chips);

    auto* row = new QHBoxLayout;
    row->setSpacing(6);
    m_input = new QLineEdit;
    m_input->setPlaceholderText(TR("Tell me what to do…"));
    m_input->setClearButtonEnabled(true);
    m_send = new QPushButton(TR("Send"));
    m_send->setObjectName("primary");
    row->addWidget(m_input, 1);
    row->addWidget(m_send);
    lay->addLayout(row);

    connect(m_send, &QPushButton::clicked, this, &ChatView::submit);
    connect(m_input, &QLineEdit::returnPressed, this, &ChatView::submit);
}

void ChatView::submit() {
    const QString t = m_input->text().trimmed();
    if (t.isEmpty()) return;
    m_input->clear();
    emit sendRequested(t);
}

void ChatView::addUser(const QString& text) {
    m_lastUser = text;
    m_follow = true;                                       // what I just said is always shown
    addBubble(text, true);
}

void ChatView::addBot(const QString& text) {
    m_lastBot = text;
    addBubble(text, false);
}

void ChatView::addBubble(const QString& text, bool user) {
    auto* b = new QLabel(text);
    b->setObjectName(user ? "bubbleUser" : "bubbleBot");
    b->setWordWrap(true);
    b->setTextFormat(Qt::PlainText);                       // never interpret what was typed or recognised as markup
    b->setTextInteractionFlags(Qt::TextSelectableByMouse);
    b->setLayoutDirection(i18n::isRtl() ? Qt::RightToLeft : Qt::LeftToRight);
    b->setAlignment(Qt::AlignLeading | Qt::AlignVCenter);
    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    if (user) {
        row->addStretch(1);
        row->addWidget(b, 5);
    } else {
        row->addWidget(b, 5);
        row->addStretch(1);
    }
    m_list->insertLayout(m_list->count() - 1, row);        // before the stretch at the end
    ++m_count;
    while (m_list->count() > 202) {                         // keep the history short
        QLayoutItem* it = m_list->takeAt(0);
        if (it->layout()) {
            while (QLayoutItem* c = it->layout()->takeAt(0)) {
                delete c->widget();
                delete c;
            }
        }
        delete it;
    }
    scrollToEnd();
}

void ChatView::scrollToEnd() {
    if (!m_follow) return;
    QTimer::singleShot(0, this, [this] {
        if (m_follow) m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->maximum());
    });
}

void ChatView::clear() {
    while (m_list->count() > 1) {
        QLayoutItem* it = m_list->takeAt(0);
        if (it->layout()) {
            while (QLayoutItem* c = it->layout()->takeAt(0)) {
                delete c->widget();
                delete c;
            }
        }
        delete it;
    }
    m_count = 0;
}

void ChatView::setChips(const QStringList& labels, const QStringList& requests) {
    delete m_chips->layout();
    qDeleteAll(m_chips->findChildren<QPushButton*>());
    auto* grid = new QGridLayout(m_chips);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(6);
    const int columns = 3;
    for (int i = 0; i < labels.size() && i < requests.size(); ++i) {
        auto* b = new QPushButton(labels[i]);
        b->setToolTip(requests[i]);
        b->setStyleSheet("padding:4px 8px;");
        connect(b, &QPushButton::clicked, this, [this, r = requests[i]] { emit sendRequested(r); });
        grid->addWidget(b, i / columns, i % columns);
    }
}

}  // namespace hr
