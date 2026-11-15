#pragma once
#include <QLineEdit>
#include <QTreeWidget>
#include <QWidget>

namespace hr {

class EffectTree : public QTreeWidget {
    Q_OBJECT
public:
    using QTreeWidget::QTreeWidget;

protected:
    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override;
    QStringList mimeTypes() const override;
};

// Lists the available effects; double-click applies one to the selected clips,
// dragging one onto a clip in the timeline applies it to that clip.
class EffectsBrowser : public QWidget {
    Q_OBJECT
public:
    explicit EffectsBrowser(QWidget* parent = nullptr);

signals:
    void applyEffect(const QString& type);

private slots:
    void filter(const QString& text);

private:
    EffectTree* m_tree;
    QLineEdit* m_search;
};

}  // namespace hr
