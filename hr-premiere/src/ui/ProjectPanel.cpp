#include "core/I18n.h"
#include "ProjectPanel.h"

#include "Icons.h"
#include "Theme.h"
#include "core/Util.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QUrl>
#include <QVBoxLayout>

namespace hr {

static const char* kMediaMime = "application/x-hr-media";

// ----------------------------------------------------------------- MediaList
MediaList::MediaList(QWidget* parent) : QListWidget(parent) {
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::CopyAction);
    setMovement(QListView::Static);
    setResizeMode(QListView::Adjust);
    setWordWrap(true);
    setSpacing(6);
    setTextElideMode(Qt::ElideMiddle);
    setContextMenuPolicy(Qt::CustomContextMenu);
}

QStringList MediaList::mimeTypes() const { return {kMediaMime}; }

QMimeData* MediaList::mimeData(const QList<QListWidgetItem*>& items) const {
    auto* m = new QMimeData;
    QStringList ids;
    for (auto* it : items) ids << it->data(Qt::UserRole).toString();
    m->setData(kMediaMime, ids.join('\n').toUtf8());
    return m;
}

void MediaList::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}
void MediaList::dragMoveEvent(QDragMoveEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}
void MediaList::dropEvent(QDropEvent* e) {
    if (!e->mimeData()->hasUrls()) return;
    QStringList paths;
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile()) paths << u.toLocalFile();
    if (!paths.isEmpty()) emit filesDropped(paths);
    e->acceptProposedAction();
}

void MediaList::mouseDoubleClickEvent(QMouseEvent* e) {
    if (!itemAt(e->pos())) emit emptyAreaDoubleClicked();
    QListWidget::mouseDoubleClickEvent(e);
}

void MediaList::paintEvent(QPaintEvent* e) {
    QListWidget::paintEvent(e);
    if (count() > 0) return;
    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing, true);
    p.drawPixmap(QRect(viewport()->width() / 2 - 24, viewport()->height() / 2 - 62, 48, 48),
                 icons::pixmap(icons::Import, theme::textDim, 48));
    p.setPen(theme::textDim);
    p.drawText(viewport()->rect().adjusted(16, 30, -16, 0), Qt::AlignCenter | Qt::TextWordWrap,
               TR("Import media to get started\nFile > Import…  (Ctrl+I)\nor drop files here"));
}

// ----------------------------------------------------------------- ProjectPanel
ProjectPanel::ProjectPanel(Project* project, MediaAssets* assets, QWidget* parent)
    : QWidget(parent), m_project(project), m_assets(assets) {
    m_list = new MediaList(this);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(TR("Search"));
    m_search->setClearButtonEnabled(true);

    m_iconBtn = new QToolButton(this);
    m_iconBtn->setIcon(icons::make(icons::IconView));
    m_iconBtn->setCheckable(true);
    m_iconBtn->setChecked(true);
    m_iconBtn->setToolTip(TR("Icon view"));
    m_listBtn = new QToolButton(this);
    m_listBtn->setIcon(icons::make(icons::ListView));
    m_listBtn->setCheckable(true);
    m_listBtn->setToolTip(TR("List view"));
    auto* importBtn = new QToolButton(this);
    importBtn->setIcon(icons::make(icons::Import));
    importBtn->setToolTip(TR("Import media (Ctrl+I)"));
    auto* delBtn = new QToolButton(this);
    delBtn->setIcon(icons::make(icons::Trash));
    delBtn->setToolTip(TR("Remove from project"));
    m_size = new QSlider(Qt::Horizontal, this);
    m_size->setRange(60, 200);
    m_size->setValue(112);
    m_size->setFixedWidth(90);
    m_count = new QLabel(this);
    m_count->setObjectName("dim");

    auto* top = new QHBoxLayout;
    top->setContentsMargins(8, 8, 8, 4);
    top->addWidget(m_search);

    auto* bottom = new QHBoxLayout;
    bottom->setContentsMargins(8, 4, 8, 6);
    bottom->addWidget(m_count);
    bottom->addStretch();
    bottom->addWidget(m_iconBtn);
    bottom->addWidget(m_listBtn);
    bottom->addWidget(m_size);
    bottom->addSpacing(8);
    bottom->addWidget(importBtn);
    bottom->addWidget(delBtn);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addLayout(top);
    lay->addWidget(m_list, 1);
    lay->addLayout(bottom);

    connect(m_iconBtn, &QToolButton::clicked, this, [this] { setIconMode(true); });
    connect(m_listBtn, &QToolButton::clicked, this, [this] { setIconMode(false); });
    connect(m_size, &QSlider::valueChanged, this, [this] { setIconMode(m_iconMode); });
    connect(importBtn, &QToolButton::clicked, this, &ProjectPanel::importRequested);
    connect(delBtn, &QToolButton::clicked, this, [this] { emit removeRequested(selectedIds()); });
    connect(m_search, &QLineEdit::textChanged, this, &ProjectPanel::applyFilter);
    connect(m_list, &MediaList::filesDropped, this, &ProjectPanel::filesDropped);
    connect(m_list, &MediaList::emptyAreaDoubleClicked, this, &ProjectPanel::importRequested);
    connect(m_list, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* it) { emit openInSource(it->data(Qt::UserRole).toString()); });
    connect(m_list, &QWidget::customContextMenuRequested, this, &ProjectPanel::contextMenu);
    connect(m_project, &Project::mediaAdded, this, &ProjectPanel::onMediaAdded);
    connect(m_project, &Project::mediaRemoved, this, &ProjectPanel::rebuild);
    connect(m_project, &Project::projectReset, this, &ProjectPanel::rebuild);
    connect(m_project, &Project::mediaChanged, this, &ProjectPanel::onAssetsUpdated);
    connect(m_assets, &MediaAssets::updated, this, &ProjectPanel::onAssetsUpdated);
    setIconMode(true);
    rebuild();
}

QStringList ProjectPanel::selectedIds() const {
    QStringList ids;
    for (auto* it : m_list->selectedItems()) ids << it->data(Qt::UserRole).toString();
    return ids;
}

QListWidgetItem* ProjectPanel::findItem(const QString& id) const {
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->data(Qt::UserRole).toString() == id) return m_list->item(i);
    return nullptr;
}

QIcon ProjectPanel::makeIcon(const MediaItem& m) const {
    const int w = 160, h = 90;
    QPixmap pm(w * 2, h * 2);
    pm.setDevicePixelRatio(2);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QRectF r(0, 0, w, h);
    QPainterPath clip;
    clip.addRoundedRect(r, 5, 5);
    p.setClipPath(clip);
    p.fillRect(r, QColor("#101010"));

    const MediaAsset a = m_assets->get(m.id);
    if (m.offline) {
        p.fillRect(r, QColor("#4a1c24"));
        p.setPen(Qt::white);
        p.drawText(r, Qt::AlignCenter, TR("Media Offline"));
    } else if (!a.poster.isNull()) {
        const QSizeF s = a.poster.size();
        const double sc = std::min(w / s.width(), h / s.height());
        const QSizeF d(s.width() * sc, s.height() * sc);
        p.drawImage(QRectF((w - d.width()) / 2, (h - d.height()) / 2, d.width(), d.height()), a.poster);
    } else if (m.kind == MediaKind::Audio) {
        QLinearGradient g(0, 0, 0, h);
        g.setColorAt(0, QColor("#1f3a31"));
        g.setColorAt(1, QColor("#14221d"));
        p.fillRect(r, g);
        if (!a.peaks10.isEmpty()) {
            p.setPen(QPen(theme::audioClip, 1.2));
            for (int x = 6; x < w - 6; ++x) {
                const int i = int(double(x - 6) / (w - 12) * (a.peaks10.size() - 1));
                const double amp = a.peaks10[i] / 255.0 * (h * 0.4);
                p.drawLine(QPointF(x, h / 2 - amp), QPointF(x, h / 2 + amp));
            }
        } else {
            p.drawPixmap(QRect(w / 2 - 16, h / 2 - 16, 32, 32), icons::pixmap(icons::Music, theme::audioClip, 32));
        }
    } else {
        p.drawPixmap(QRect(w / 2 - 16, h / 2 - 16, 32, 32),
                     icons::pixmap(m.kind == MediaKind::Image ? icons::ImageFile : icons::Film, theme::textDim, 32));
    }
    // duration badge
    if (m.kind != MediaKind::Image) {
        const QString d = formatDuration(m.duration);
        QFont f = p.font();
        f.setPixelSize(11);
        f.setBold(true);
        p.setFont(f);
        const QFontMetrics fm(f);
        const QRectF b(w - fm.horizontalAdvance(d) - 12, h - 19, fm.horizontalAdvance(d) + 8, 15);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 170));
        p.drawRoundedRect(b, 3, 3);
        p.setPen(Qt::white);
        p.drawText(b, Qt::AlignCenter, d);
    }
    // kind marker
    p.setClipping(false);
    return QIcon(pm);
}

void ProjectPanel::updateItem(QListWidgetItem* it, const MediaItem& m) {
    it->setIcon(makeIcon(m));
    if (m_iconMode) {
        it->setText(m.name);
    } else {
        QString info = m.kind == MediaKind::Audio ? QString("%1 Hz").arg(m.sampleRate)
                       : m.kind == MediaKind::Image ? QString("%1 × %2").arg(m.width).arg(m.height)
                                                    : QString(TR("%1 × %2  ·  %3 fps")).arg(m.width).arg(m.height).arg(m.fps, 0, 'f', m.fps == std::floor(m.fps) ? 0 : 2);
        it->setText(QString("%1\n%2  ·  %3").arg(m.name, formatDuration(m.duration), info));
    }
    it->setToolTip(m.path);
}

void ProjectPanel::rebuild() {
    m_list->clear();
    for (const MediaItem& m : m_project->media()) {
        auto* it = new QListWidgetItem;
        it->setData(Qt::UserRole, m.id);
        updateItem(it, m);
        m_list->addItem(it);
    }
    applyFilter();
}

void ProjectPanel::onMediaAdded(const QString& id) {
    if (findItem(id)) return;
    if (const MediaItem* m = m_project->mediaById(id)) {
        auto* it = new QListWidgetItem;
        it->setData(Qt::UserRole, m->id);
        updateItem(it, *m);
        m_list->addItem(it);
        m_assets->request(*m);
        applyFilter();
    }
}

void ProjectPanel::onAssetsUpdated(const QString& id) {
    if (auto* it = findItem(id))
        if (const MediaItem* m = m_project->mediaById(id)) updateItem(it, *m);
}

void ProjectPanel::applyFilter() {
    const QString q = m_search->text().trimmed();
    int visible = 0;
    for (int i = 0; i < m_list->count(); ++i) {
        auto* it = m_list->item(i);
        const bool show = q.isEmpty() || it->text().contains(q, Qt::CaseInsensitive);
        it->setHidden(!show);
        if (show) ++visible;
    }
    m_count->setText(QString(TR("%1 item%2")).arg(visible).arg(visible == 1 ? "" : "s"));
}

void ProjectPanel::setIconMode(bool icons) {
    m_iconMode = icons;
    m_iconBtn->setChecked(icons);
    m_listBtn->setChecked(!icons);
    const int s = m_size->value();
    if (icons) {
        m_list->setViewMode(QListView::IconMode);
        m_list->setIconSize(QSize(s, int(s * 0.5625)));
        m_list->setGridSize(QSize(s + 20, int(s * 0.5625) + 40));
        m_list->setFlow(QListView::LeftToRight);
        m_list->setWrapping(true);
    } else {
        m_list->setViewMode(QListView::ListMode);
        m_list->setIconSize(QSize(64, 36));
        m_list->setGridSize(QSize());
        m_list->setFlow(QListView::TopToBottom);
        m_list->setWrapping(false);
    }
    m_list->setDragEnabled(true);
    for (int i = 0; i < m_list->count(); ++i) {
        auto* it = m_list->item(i);
        if (const MediaItem* m = m_project->mediaById(it->data(Qt::UserRole).toString())) updateItem(it, *m);
    }
    applyFilter();
}

void ProjectPanel::contextMenu(const QPoint& pos) {
    QMenu menu(this);
    const QStringList ids = selectedIds();
    if (ids.isEmpty()) {
        menu.addAction(TR("Import…"), this, &ProjectPanel::importRequested);
    } else {
        menu.addAction(TR("Open in Source Monitor"), this, [this, ids] { emit openInSource(ids.first()); });
        menu.addAction(TR("Insert at Playhead"), this, [this, ids] { emit insertRequested(ids); });
        menu.addAction(TR("Overwrite at Playhead"), this, [this, ids] { emit overwriteRequested(ids); });
        menu.addSeparator();
        menu.addAction(TR("Reveal in Folder"), this, [this, ids] {
            if (const MediaItem* m = m_project->mediaById(ids.first()))
                QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m->path).absolutePath()));
        });
        menu.addAction(TR("Remove from Project"), this, [this, ids] { emit removeRequested(ids); });
    }
    menu.exec(m_list->viewport()->mapToGlobal(pos));
}

}  // namespace hr
