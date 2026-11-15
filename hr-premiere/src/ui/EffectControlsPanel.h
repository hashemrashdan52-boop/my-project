#pragma once
#include "core/Project.h"

#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

namespace hr {

// Edits the effects (Motion, Opacity, Volume, Text and any added effects) of the
// selected clip.  The UI is generated from the effect registry.
class EffectControlsPanel : public QScrollArea {
    Q_OBJECT
public:
    explicit EffectControlsPanel(Project* project, QWidget* parent = nullptr);

private slots:
    void onSelectionChanged();
    void onTimelineChanged();

private:
    struct Row {
        QString type, key;
        const ParamDef* def = nullptr;
        QWidget* editor = nullptr;
        QWidget* slider = nullptr;
        QWidget* label = nullptr;
    };

    void rebuild();
    void syncValues();
    QString signature(const Clip& c) const;
    void applyParam(const QString& type, const QString& key, const QVariant& v, const QString& label);
    void setEnabled(const QString& type, bool on);
    void resetEffect(const QString& type);
    void removeEffect(const QString& type);
    QWidget* buildCard(const Clip& c, const EffectInstance& e, const EffectDef& d);

    Project* m_project;
    QWidget* m_body;
    QVBoxLayout* m_layout;
    QString m_clipId;
    QString m_signature;
    QVector<Row> m_rows;
    bool m_syncing = false;
};

}  // namespace hr
