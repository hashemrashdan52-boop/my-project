#include "EffectsBrowser.h"

#include "Icons.h"
#include "core/Effects.h"

#include <QMimeData>
#include <QVBoxLayout>

namespace hr {

static const char* kEffectMime = "application/x-hr-effect";

QStringList EffectTree::mimeTypes() const { return {kEffectMime}; }

QMimeData* EffectTree::mimeData(const QList<QTreeWidgetItem*>& items) const {
    auto* m = new QMimeData;
    if (!items.isEmpty()) m->setData(kEffectMime, items.first()->data(0, Qt::UserRole).toString().toUtf8());
    return m;
}

EffectsBrowser::EffectsBrowser(QWidget* parent) : QWidget(parent) {
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText("Search effects");
    m_search->setClearButtonEnabled(true);
    m_tree = new EffectTree(this);
    m_tree->setHeaderHidden(true);
    m_tree->setDragEnabled(true);
    m_tree->setDragDropMode(QAbstractItemView::DragOnly);
    m_tree->setIndentation(16);

    for (bool video : {true, false}) {
        auto* root = new QTreeWidgetItem(m_tree, {video ? "Video Effects" : "Audio Effects"});
        root->setIcon(0, icons::make(video ? icons::Film : icons::Music));
        QFont f = root->font(0);
        f.setBold(true);
        root->setFont(0, f);
        root->setFlags(Qt::ItemIsEnabled);
        for (const QString& cat : EffectRegistry::categories(video)) {
            auto* catItem = new QTreeWidgetItem(root, {cat});
            catItem->setFlags(Qt::ItemIsEnabled);
            for (const EffectDef& d : EffectRegistry::all()) {
                if (d.intrinsic || d.video != video || d.category != cat) continue;
                auto* it = new QTreeWidgetItem(catItem, {d.name});
                it->setData(0, Qt::UserRole, d.type);
                it->setIcon(0, icons::make(icons::Fx));
                it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
            }
        }
    }
    m_tree->expandAll();

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->setSpacing(6);
    lay->addWidget(m_search);
    lay->addWidget(m_tree, 1);

    connect(m_search, &QLineEdit::textChanged, this, &EffectsBrowser::filter);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it) {
        const QString type = it->data(0, Qt::UserRole).toString();
        if (!type.isEmpty()) emit applyEffect(type);
    });
}

void EffectsBrowser::filter(const QString& text) {
    const QString q = text.trimmed();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto* root = m_tree->topLevelItem(i);
        bool rootVisible = false;
        for (int j = 0; j < root->childCount(); ++j) {
            auto* cat = root->child(j);
            bool catVisible = false;
            for (int k = 0; k < cat->childCount(); ++k) {
                auto* it = cat->child(k);
                const bool show = q.isEmpty() || it->text(0).contains(q, Qt::CaseInsensitive);
                it->setHidden(!show);
                catVisible = catVisible || show;
            }
            cat->setHidden(!catVisible);
            rootVisible = rootVisible || catVisible;
        }
        root->setHidden(!rootVisible);
    }
}

}  // namespace hr
