#include "ToolsBar.h"
#include "core/I18n.h"

#include "Icons.h"
#include "Theme.h"

#include <QVBoxLayout>

namespace hr {

ToolsBar::ToolsBar(QWidget* parent) : QWidget(parent) {
    setFixedWidth(44);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 36, 4, 4);
    lay->setSpacing(3);
    struct T { Tool tool; icons::Name icon; const char* tip; };
    const T tools[] = {
        {Tool::Selection, icons::Selection, "Selection Tool (V)"},
        {Tool::TrackForward, icons::TrackForward, "Track Select Forward Tool (A)"},
        {Tool::Ripple, icons::Ripple, "Ripple Edit Tool (B)"},
        {Tool::Razor, icons::Razor, "Razor Tool (C)"},
        {Tool::Hand, icons::Hand, "Hand Tool (H)"},
        {Tool::Zoom, icons::Zoom, "Zoom Tool (Z)"},
        {Tool::Type, icons::Type, "Type Tool (T) – click a video track to add a title"},
    };
    m_group.setExclusive(true);
    for (const T& t : tools) {
        auto* b = new QToolButton(this);
        b->setIcon(icons::make(t.icon));
        b->setIconSize(QSize(20, 20));
        b->setFixedSize(36, 32);
        b->setCheckable(true);
        b->setToolTip(TR(t.tip));
        lay->addWidget(b);
        m_group.addButton(b);
        m_buttons << b;
        connect(b, &QToolButton::clicked, this, [this, tool = t.tool] { emit toolSelected(tool); });
    }
    lay->addStretch();
    m_buttons.first()->setChecked(true);
}

void ToolsBar::setTool(Tool t) {
    const int i = int(t);
    if (i >= 0 && i < m_buttons.size()) m_buttons[i]->setChecked(true);
}

}  // namespace hr
