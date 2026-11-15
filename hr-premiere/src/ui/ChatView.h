#pragma once
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

namespace hr {

// The conversation with the assistant: bubbles, a line to type in, and quick buttons for the common requests.
class ChatView : public QWidget {
    Q_OBJECT
public:
    explicit ChatView(QWidget* parent = nullptr);

    void addUser(const QString& text);
    void addBot(const QString& text);
    // Quick buttons: what is written on them and what they send.
    void setChips(const QStringList& labels, const QStringList& requests);
    int messageCount() const { return m_count; }
    QString lastBotText() const { return m_lastBot; }
    QString lastUserText() const { return m_lastUser; }
    void clear();

signals:
    void sendRequested(const QString& text);

private:
    void addBubble(const QString& text, bool user);
    void scrollToEnd();
    void submit();

    QScrollArea* m_scroll = nullptr;
    QWidget* m_content = nullptr;
    QVBoxLayout* m_list = nullptr;
    QLineEdit* m_input = nullptr;
    QPushButton* m_send = nullptr;
    QWidget* m_chips = nullptr;
    int m_count = 0;
    bool m_follow = true;            // the view stays at the newest message (until the user scrolls up)
    QString m_lastBot, m_lastUser;
};

}  // namespace hr
