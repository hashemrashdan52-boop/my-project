#pragma once
#include "TimelineWidget.h"

#include <QButtonGroup>
#include <QToolButton>
#include <QWidget>

namespace hr {

// The vertical tool palette next to the timeline.
class ToolsBar : public QWidget {
    Q_OBJECT
public:
    explicit ToolsBar(QWidget* parent = nullptr);
    void setTool(Tool t);

signals:
    void toolSelected(Tool t);

private:
    QButtonGroup m_group;
    QVector<QToolButton*> m_buttons;
};

}  // namespace hr
