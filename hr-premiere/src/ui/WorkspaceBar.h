#pragma once
#include <QLabel>
#include <QWidget>

namespace hr {

// The strip under the menu with the HR logo and the workspace tabs.
class WorkspaceBar : public QWidget {
    Q_OBJECT
public:
    explicit WorkspaceBar(QWidget* parent = nullptr);
    void setCurrent(const QString& name);
    void setProjectName(const QString& name, bool dirty);
    QStringList workspaces() const { return m_names; }

signals:
    void workspaceSelected(const QString& name);
    void logoClicked();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    QRectF tabRect(int i) const;
    QStringList m_names{"Assembly", "Editing", "Color", "Effects", "Audio", "AI"};
    int m_current = 1, m_hover = -1;
    QString m_project = "Untitled";
    bool m_dirty = false;
};

}  // namespace hr
