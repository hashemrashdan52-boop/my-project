#include "Effects.h"

#include <QHash>
#include <QtGlobal>

namespace hr {

#if defined(Q_OS_WIN)
static const char* kDefaultTitleFont = "Segoe UI";
#elif defined(Q_OS_MAC)
static const char* kDefaultTitleFont = "Helvetica Neue";
#else
static const char* kDefaultTitleFont = "Sans Serif";
#endif

static ParamDef num(const char* key, const char* label, double min, double max, double def,
                    double step = 1, int decimals = 1, const char* unit = "") {
    ParamDef p;
    p.key = key; p.label = label; p.kind = ParamDef::Number;
    p.min = min; p.max = max; p.def = def; p.step = step; p.decimals = decimals; p.unit = unit;
    return p;
}
static ParamDef boolean(const char* key, const char* label, bool def) {
    ParamDef p;
    p.key = key; p.label = label; p.kind = ParamDef::Bool; p.defBool = def;
    return p;
}
static ParamDef text(const char* key, const char* label, const char* def) {
    ParamDef p;
    p.key = key; p.label = label; p.kind = ParamDef::Text; p.defText = def;
    return p;
}
static ParamDef color(const char* key, const char* label, const char* def) {
    ParamDef p;
    p.key = key; p.label = label; p.kind = ParamDef::Color; p.defText = def;
    return p;
}
static ParamDef font(const char* key, const char* label, const char* def) {
    ParamDef p;
    p.key = key; p.label = label; p.kind = ParamDef::Font; p.defText = def;
    return p;
}
static ParamDef choice(const char* key, const char* label, const QStringList& c, int def) {
    ParamDef p;
    p.key = key; p.label = label; p.kind = ParamDef::Choice; p.choices = c;
    p.defText = c.value(def);
    return p;
}

static QVector<EffectDef> build() {
    QVector<EffectDef> v;
    auto add = [&](const char* type, const char* name, const char* cat, bool video, bool intrinsic,
                   QVector<ParamDef> params) {
        EffectDef d;
        d.type = type; d.name = name; d.category = cat; d.video = video; d.intrinsic = intrinsic;
        d.params = std::move(params);
        v.push_back(d);
    };

    // ---- intrinsic effects (present on every clip of that kind) -------------
    add("motion", "Motion", "", true, true, {
        num("position_x", "Position X", -200, 200, 0, 0.5, 1, "%"),
        num("position_y", "Position Y", -200, 200, 0, 0.5, 1, "%"),
        num("scale", "Scale", 0, 800, 100, 1, 1, "%"),
        num("rotation", "Rotation", -3600, 3600, 0, 1, 1, "°"),
        // movement over the length of the clip (0 = none): the picture slowly zooms / drifts while it plays
        num("zoom_amount", "Zoom During Clip", -90, 300, 0, 0.5, 1, "%"),
        num("pan_x", "Drift X During Clip", -100, 100, 0, 0.5, 1, "%"),
        num("pan_y", "Drift Y During Clip", -100, 100, 0, 0.5, 1, "%"),
    });
    add("opacity", "Opacity", "", true, true, {
        num("opacity", "Opacity", 0, 100, 100, 1, 1, "%"),
        num("fade_in", "Fade In", 0, 60, 0, 0.1, 2, "s"),
        num("fade_out", "Fade Out", 0, 60, 0, 0.1, 2, "s"),
    });
    add("volume", "Volume", "", false, true, {
        num("level", "Level", -60, 20, 0, 0.5, 1, "dB"),
        num("fade_in", "Fade In", 0, 60, 0, 0.1, 2, "s"),
        num("fade_out", "Fade Out", 0, 60, 0, 0.1, 2, "s"),
    });
    add("text", "Text", "", true, true, {
        text("text", "Text", "New Title"),
        font("font", "Font", kDefaultTitleFont),
        num("size", "Size", 8, 600, 96, 1, 0, "px"),
        color("color", "Fill", "#ffffff"),
        boolean("bold", "Bold", true),
        boolean("italic", "Italic", false),
        boolean("shadow", "Shadow", true),
        choice("align", "Align", {"Left", "Center", "Right"}, 1),
    });

    // ---- video effects ------------------------------------------------------
    add("brightness_contrast", "Brightness & Contrast", "Color Correction", true, false, {
        num("brightness", "Brightness", -100, 100, 0),
        num("contrast", "Contrast", -100, 100, 0),
    });
    add("color_correction", "Color Correction", "Color Correction", true, false, {
        num("exposure", "Exposure", -3, 3, 0, 0.05, 2, "EV"),
        num("saturation", "Saturation", 0, 200, 100, 1, 0, "%"),
        num("temperature", "Temperature", -100, 100, 0),
        num("tint", "Tint", -100, 100, 0),
    });
    add("black_white", "Black & White", "Color Correction", true, false, {
        num("amount", "Amount", 0, 100, 100, 1, 0, "%"),
    });
    add("invert", "Invert", "Color Correction", true, false, {
        num("amount", "Amount", 0, 100, 100, 1, 0, "%"),
    });
    add("gaussian_blur", "Gaussian Blur", "Blur & Sharpen", true, false, {
        num("blurriness", "Blurriness", 0, 100, 10, 0.5, 1, "px"),
    });
    add("sharpen", "Sharpen", "Blur & Sharpen", true, false, {
        num("amount", "Amount", 0, 100, 40, 1, 0),
    });
    add("mirror", "Mirror", "Transform", true, false, {
        boolean("horizontal", "Horizontal", true),
        boolean("vertical", "Vertical", false),
    });
    add("crop", "Crop", "Transform", true, false, {
        num("left", "Left", 0, 100, 0, 0.5, 1, "%"),
        num("top", "Top", 0, 100, 0, 0.5, 1, "%"),
        num("right", "Right", 0, 100, 0, 0.5, 1, "%"),
        num("bottom", "Bottom", 0, 100, 0, 0.5, 1, "%"),
    });
    add("vignette", "Vignette", "Stylize", true, false, {
        num("amount", "Amount", 0, 100, 50, 1, 0),
        num("size", "Size", 10, 150, 80, 1, 0, "%"),
    });

    // ---- audio effects ------------------------------------------------------
    add("balance", "Balance", "Audio Effects", false, false, {
        num("pan", "Pan", -100, 100, 0, 1, 0),
    });
    return v;
}

const QVector<EffectDef>& EffectRegistry::all() {
    static const QVector<EffectDef> defs = build();
    return defs;
}

const EffectDef* EffectRegistry::find(const QString& type) {
    static const QHash<QString, int> index = [] {
        QHash<QString, int> h;
        const auto& a = all();
        for (int i = 0; i < a.size(); ++i) h.insert(a[i].type, i);
        return h;
    }();
    auto it = index.constFind(type);
    return it == index.constEnd() ? nullptr : &all()[it.value()];
}

EffectInstance EffectRegistry::make(const QString& type) {
    EffectInstance e;
    e.type = type;
    if (const EffectDef* d = find(type)) {
        for (const ParamDef& p : d->params) {
            switch (p.kind) {
            case ParamDef::Number: e.params.insert(p.key, p.def); break;
            case ParamDef::Bool: e.params.insert(p.key, p.defBool); break;
            default: e.params.insert(p.key, p.defText); break;
            }
        }
    }
    return e;
}

QStringList EffectRegistry::categories(bool video) {
    QStringList out;
    for (const EffectDef& d : all())
        if (!d.intrinsic && d.video == video && !out.contains(d.category)) out << d.category;
    return out;
}

double EffectInstance::num(const QString& key) const {
    auto it = params.constFind(key);
    if (it != params.constEnd()) return it->toDouble();
    if (const EffectDef* d = EffectRegistry::find(type))
        for (const ParamDef& p : d->params)
            if (p.key == key) return p.def;
    return 0;
}

bool EffectInstance::flag(const QString& key) const {
    auto it = params.constFind(key);
    if (it != params.constEnd()) return it->toBool();
    if (const EffectDef* d = EffectRegistry::find(type))
        for (const ParamDef& p : d->params)
            if (p.key == key) return p.defBool;
    return false;
}

QString EffectInstance::text(const QString& key) const {
    auto it = params.constFind(key);
    if (it != params.constEnd()) return it->toString();
    if (const EffectDef* d = EffectRegistry::find(type))
        for (const ParamDef& p : d->params)
            if (p.key == key) return p.defText;
    return {};
}

}  // namespace hr
