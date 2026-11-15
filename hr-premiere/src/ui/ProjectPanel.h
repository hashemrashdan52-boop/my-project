#pragma once
#include "core/Project.h"
#include "media/MediaAssets.h"

#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSlider>
#include <QToolButton>
#include <QWidget>

namespace hr {

class MediaList : public QListWidget {
    Q_OBJECT
public:
    explicit MediaList(QWidget* parent = nullptr);

signals:
    void filesDropped(const QStringList& paths);
    void emptyAreaDoubleClicked();

protected:
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;
    QStringList mimeTypes() const override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dragMoveEvent(QDragMoveEvent*) override;
    void dropEvent(QDropEvent*) override;
    void paintEvent(QPaintEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
};

// The Project panel: the bin of imported media.
class ProjectPanel : public QWidget {
    Q_OBJECT
public:
    ProjectPanel(Project* project, MediaAssets* assets, QWidget* parent = nullptr);

    QStringList selectedIds() const;

signals:
    void importRequested();
    void filesDropped(const QStringList& paths);
    void openInSource(const QString& id);
    void insertRequested(const QStringList& ids);
    void overwriteRequested(const QStringList& ids);
    void removeRequested(const QStringList& ids);

private slots:
    void rebuild();
    void onMediaAdded(const QString& id);
    void onAssetsUpdated(const QString& id);
    void applyFilter();
    void setIconMode(bool icons);
    void contextMenu(const QPoint& pos);

private:
    QIcon makeIcon(const MediaItem& m) const;
    void updateItem(QListWidgetItem* it, const MediaItem& m);
    QListWidgetItem* findItem(const QString& id) const;

    Project* m_project;
    MediaAssets* m_assets;
    MediaList* m_list;
    QLineEdit* m_search;
    QLabel* m_count;
    QSlider* m_size;
    QToolButton* m_iconBtn;
    QToolButton* m_listBtn;
    bool m_iconMode = true;
};

}  // namespace hr
