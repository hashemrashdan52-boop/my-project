#include "core/I18n.h"
#include "EffectControlsPanel.h"

#include "Icons.h"
#include "ScrubLabel.h"
#include "Theme.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QToolButton>
#include <cmath>

namespace hr {

static QString colorButtonStyle(const QString& hex) {
    return QString("QPushButton { background:%1; border:1px solid #555; border-radius:3px; min-width:56px; max-width:56px; min-height:18px; }").arg(hex);
}

EffectControlsPanel::EffectControlsPanel(Project* project, QWidget* parent) : QScrollArea(parent), m_project(project) {
    setWidgetResizable(true);
    m_body = new QWidget;
    m_layout = new QVBoxLayout(m_body);
    m_layout->setContentsMargins(8, 8, 8, 8);
    m_layout->setSpacing(8);
    setWidget(m_body);
    connect(m_project, &Project::selectionChanged, this, &EffectControlsPanel::onSelectionChanged);
    connect(m_project, &Project::timelineChanged, this, &EffectControlsPanel::onTimelineChanged);
    connect(m_project, &Project::projectReset, this, &EffectControlsPanel::onSelectionChanged);
    rebuild();
}

QString EffectControlsPanel::signature(const Clip& c) const {
    QString s = c.id;
    for (const auto& e : c.effects) s += "|" + e.type + (e.enabled ? "+" : "-");
    return s;
}

void EffectControlsPanel::onSelectionChanged() { rebuild(); }

void EffectControlsPanel::onTimelineChanged() {
    const Clip* c = m_project->timeline().clip(m_clipId);
    if (!c) {
        if (!m_clipId.isEmpty()) rebuild();
        return;
    }
    if (signature(*c) != m_signature) rebuild();
    else syncValues();
}

void EffectControlsPanel::rebuild() {
    // clear
    m_rows.clear();
    while (QLayoutItem* it = m_layout->takeAt(0)) {
        if (QWidget* w = it->widget()) w->deleteLater();
        delete it;
    }
    m_clipId = m_project->primarySelection();
    const Clip* c = m_project->timeline().clip(m_clipId);
    if (!c) {
        m_clipId.clear();
        m_signature.clear();
        auto* hint = new QLabel(TR("Select a clip on the timeline\nto edit its effects."));
        hint->setAlignment(Qt::AlignCenter);
        hint->setObjectName("dim");
        m_layout->addStretch();
        m_layout->addWidget(hint);
        m_layout->addStretch();
        return;
    }
    m_signature = signature(*c);

    const bool isAudio = c->mediaKind == MediaKind::Audio && c->kind == ClipKind::Media;
    auto* head = new QLabel(QString("<b>%1</b><br><span style='color:%2'>%3</span>")
                                .arg(c->name.toHtmlEscaped(), theme::textDim.name(),
                                     c->kind == ClipKind::Title ? TR("Title") : (isAudio ? TR("Audio clip") : (c->mediaKind == MediaKind::Image ? TR("Still image") : TR("Video clip")))));
    head->setTextFormat(Qt::RichText);
    m_layout->addWidget(head);
    if (m_project->selection().size() > 1) {
        auto* multi = new QLabel(QString(TR("%1 clips selected – changes apply to all of them")).arg(m_project->selection().size()));
        multi->setObjectName("dim");
        multi->setWordWrap(true);
        m_layout->addWidget(multi);
    }
    for (const EffectInstance& e : c->effects) {
        const EffectDef* d = EffectRegistry::find(e.type);
        if (!d) continue;
        m_layout->addWidget(buildCard(*c, e, *d));
    }
    m_layout->addStretch();
    syncValues();
}

QWidget* EffectControlsPanel::buildCard(const Clip& c, const EffectInstance& e, const EffectDef& d) {
    auto* card = new QFrame;
    card->setObjectName("card");
    auto* v = new QVBoxLayout(card);
    v->setContentsMargins(8, 6, 8, 8);
    v->setSpacing(6);

    auto* hdr = new QHBoxLayout;
    auto* fxBox = new QCheckBox;
    fxBox->setChecked(e.enabled);
    fxBox->setToolTip(TR("Enable / disable this effect"));
    auto* title = new QLabel(TR(d.name));
    title->setStyleSheet("font-weight:600;");
    hdr->addWidget(fxBox);
    hdr->addWidget(title);
    hdr->addStretch();
    auto* reset = new QToolButton;
    reset->setText(TR("Reset"));
    reset->setToolTip(TR("Reset this effect to its defaults"));
    hdr->addWidget(reset);
    if (!d.intrinsic) {
        auto* rm = new QToolButton;
        rm->setIcon(icons::make(icons::Trash));
        rm->setToolTip(TR("Remove this effect"));
        hdr->addWidget(rm);
        connect(rm, &QToolButton::clicked, this, [this, t = d.type] { removeEffect(t); });
    }
    v->addLayout(hdr);
    connect(fxBox, &QCheckBox::toggled, this, [this, t = d.type](bool on) { if (!m_syncing) setEnabled(t, on); });
    connect(reset, &QToolButton::clicked, this, [this, t = d.type] { resetEffect(t); });

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(5);
    grid->setColumnStretch(2, 1);
    int row = 0;
    for (const ParamDef& p : d.params) {
        Row r;
        r.type = d.type;
        r.key = p.key;
        r.def = &p;
        const QString type = d.type, key = p.key, label = p.label;
        switch (p.kind) {
        case ParamDef::Number: {
            auto* name = new ScrubLabel(TR(p.label));
            name->setScrubRange(p.min, p.max, p.step * 0.5);
            name->setObjectName("accentText");
            auto* spin = new QDoubleSpinBox;
            spin->setRange(p.min, p.max);
            spin->setDecimals(p.decimals);
            spin->setSingleStep(p.step);
            spin->setSuffix(p.unit.isEmpty() ? QString() : QString(" ") + p.unit);
            spin->setFixedWidth(86);
            spin->setKeyboardTracking(false);
            auto* slider = new QSlider(Qt::Horizontal);
            slider->setRange(0, 1000);
            const double min = p.min, max = p.max;
            auto toSlider = [min, max](double val) { return int(std::lround((val - min) / (max - min) * 1000.0)); };
            connect(spin, &QDoubleSpinBox::valueChanged, this, [=](double val) {
                slider->blockSignals(true);
                slider->setValue(toSlider(val));
                slider->blockSignals(false);
                name->setValue(val);
                if (!m_syncing) applyParam(type, key, val, label);
            });
            connect(slider, &QSlider::valueChanged, this, [=](int sv) {
                const double val = min + (max - min) * sv / 1000.0;
                const double step = spin->singleStep() / 10.0;
                spin->setValue(std::round(val / step) * step);
            });
            connect(name, &ScrubLabel::scrubbed, spin, &QDoubleSpinBox::setValue);
            grid->addWidget(name, row, 0);
            grid->addWidget(spin, row, 1);
            grid->addWidget(slider, row, 2);
            r.editor = spin;
            r.slider = slider;
            r.label = name;
            break;
        }
        case ParamDef::Bool: {
            auto* cb = new QCheckBox;
            connect(cb, &QCheckBox::toggled, this, [=](bool on) { if (!m_syncing) applyParam(type, key, on, label); });
            grid->addWidget(new QLabel(TR(p.label)), row, 0);
            grid->addWidget(cb, row, 1);
            r.editor = cb;
            break;
        }
        case ParamDef::Text: {
            grid->addWidget(new QLabel(TR(p.label)), row, 0, Qt::AlignTop);
            if (p.key == "text") {
                auto* te = new QPlainTextEdit;
                te->setFixedHeight(64);
                connect(te, &QPlainTextEdit::textChanged, this, [=] { if (!m_syncing) applyParam(type, key, te->toPlainText(), label); });
                grid->addWidget(te, row, 1, 1, 2);
                r.editor = te;
            } else {
                auto* le = new QLineEdit;
                connect(le, &QLineEdit::textEdited, this, [=](const QString& s) { applyParam(type, key, s, label); });
                grid->addWidget(le, row, 1, 1, 2);
                r.editor = le;
            }
            break;
        }
        case ParamDef::Color: {
            auto* btn = new QPushButton;
            connect(btn, &QPushButton::clicked, this, [=] {
                const Clip* cc = m_project->timeline().clip(m_clipId);
                const EffectInstance* ee = cc ? cc->effect(type) : nullptr;
                const QColor start(ee ? ee->text(key) : "#ffffff");
                const QColor col = QColorDialog::getColor(start, this, TR("Choose a color"));
                if (col.isValid()) applyParam(type, key, col.name(), label);
            });
            grid->addWidget(new QLabel(TR(p.label)), row, 0);
            grid->addWidget(btn, row, 1);
            r.editor = btn;
            break;
        }
        case ParamDef::Choice: {
            auto* cb = new QComboBox;
            for (const QString& c : p.choices) cb->addItem(TR(c), c);        // shown translated, stored in English
            connect(cb, &QComboBox::currentIndexChanged, this, [=](int) { if (!m_syncing) applyParam(type, key, cb->currentData().toString(), label); });
            grid->addWidget(new QLabel(TR(p.label)), row, 0);
            grid->addWidget(cb, row, 1, 1, 2);
            r.editor = cb;
            break;
        }
        case ParamDef::Font: {
            auto* fc = new QFontComboBox;
            connect(fc, &QFontComboBox::currentFontChanged, this, [=](const QFont& f) { if (!m_syncing) applyParam(type, key, f.family(), label); });
            grid->addWidget(new QLabel(TR(p.label)), row, 0);
            grid->addWidget(fc, row, 1, 1, 2);
            r.editor = fc;
            break;
        }
        }
        m_rows.push_back(r);
        ++row;
    }
    v->addLayout(grid);
    Q_UNUSED(c);
    return card;
}

void EffectControlsPanel::syncValues() {
    const Clip* c = m_project->timeline().clip(m_clipId);
    if (!c) return;
    m_syncing = true;
    for (const Row& r : m_rows) {
        const EffectInstance* e = c->effect(r.type);
        if (!e || !r.editor) continue;
        switch (r.def->kind) {
        case ParamDef::Number: {
            auto* spin = static_cast<QDoubleSpinBox*>(r.editor);
            QSignalBlocker b(spin);
            spin->setValue(e->num(r.key));
            static_cast<QSlider*>(r.slider)->setValue(int(std::lround((e->num(r.key) - r.def->min) / (r.def->max - r.def->min) * 1000.0)));
            static_cast<ScrubLabel*>(r.label)->setValue(e->num(r.key));
            break;
        }
        case ParamDef::Bool: static_cast<QCheckBox*>(r.editor)->setChecked(e->flag(r.key)); break;
        case ParamDef::Text:
            if (auto* te = qobject_cast<QPlainTextEdit*>(r.editor)) {
                if (te->toPlainText() != e->text(r.key)) te->setPlainText(e->text(r.key));
            } else if (auto* le = qobject_cast<QLineEdit*>(r.editor)) {
                if (le->text() != e->text(r.key)) le->setText(e->text(r.key));
            }
            break;
        case ParamDef::Color: static_cast<QPushButton*>(r.editor)->setStyleSheet(colorButtonStyle(e->text(r.key))); break;
        case ParamDef::Choice: { auto* cb = static_cast<QComboBox*>(r.editor); cb->setCurrentIndex(std::max(0, cb->findData(e->text(r.key)))); break; }
        case ParamDef::Font: static_cast<QFontComboBox*>(r.editor)->setCurrentFont(QFont(e->text(r.key))); break;
        }
    }
    m_syncing = false;
}

void EffectControlsPanel::applyParam(const QString& type, const QString& key, const QVariant& v, const QString& label) {
    const QSet<QString> ids = m_project->selection().isEmpty() ? QSet<QString>{m_clipId} : m_project->selection();
    m_project->edit(QString(TR("Change %1")).arg(label),
                    [&](Timeline& t) {
                        for (const QString& id : ids)
                            if (Clip* c = t.clip(id))
                                if (EffectInstance* e = c->effect(type)) e->set(key, v);
                    },
                    QString("fx:%1:%2:%3").arg(m_clipId, type, key));
}

void EffectControlsPanel::setEnabled(const QString& type, bool on) {
    const QSet<QString> ids = m_project->selection().isEmpty() ? QSet<QString>{m_clipId} : m_project->selection();
    m_project->edit(on ? TR("Enable effect") : TR("Disable effect"), [&](Timeline& t) {
        for (const QString& id : ids)
            if (Clip* c = t.clip(id))
                if (EffectInstance* e = c->effect(type)) e->enabled = on;
    });
}

void EffectControlsPanel::resetEffect(const QString& type) {
    const QSet<QString> ids = m_project->selection().isEmpty() ? QSet<QString>{m_clipId} : m_project->selection();
    m_project->edit(TR("Reset effect"), [&](Timeline& t) {
        for (const QString& id : ids)
            if (Clip* c = t.clip(id))
                if (EffectInstance* e = c->effect(type)) {
                    const bool en = e->enabled;
                    QVariantMap keepText;
                    if (type == "text") keepText.insert("text", e->params.value("text"));
                    *e = EffectRegistry::make(type);
                    e->enabled = en;
                    for (auto it = keepText.begin(); it != keepText.end(); ++it) e->params.insert(it.key(), it.value());
                }
    });
}

void EffectControlsPanel::removeEffect(const QString& type) {
    const QSet<QString> ids = m_project->selection().isEmpty() ? QSet<QString>{m_clipId} : m_project->selection();
    m_project->edit(TR("Remove effect"), [&](Timeline& t) {
        for (const QString& id : ids)
            if (Clip* c = t.clip(id))
                for (int i = c->effects.size() - 1; i >= 0; --i)
                    if (c->effects[i].type == type) c->effects.removeAt(i);
    });
}

}  // namespace hr
