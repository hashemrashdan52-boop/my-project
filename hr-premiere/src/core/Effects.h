#pragma once
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

namespace hr {

// Description of one editable parameter of an effect.  The Effect Controls panel
// is generated entirely from these definitions, so adding an effect to the
// registry is enough to make it show up in the UI, the renderer and project files.
struct ParamDef {
    enum Kind { Number, Bool, Text, Color, Choice, Font };
    QString key;
    QString label;
    Kind kind = Number;
    double min = 0, max = 100, def = 0;
    double step = 1;
    int decimals = 1;
    QString unit;
    QStringList choices;   // Choice
    QString defText;       // Text / Color / Choice / Font default
    bool defBool = false;  // Bool default
};

struct EffectDef {
    QString type;
    QString name;
    QString category;
    bool video = true;       // false -> audio effect
    bool intrinsic = false;  // always present on a clip (Motion, Opacity, Volume, Text)
    QVector<ParamDef> params;
};

struct EffectInstance {
    QString type;
    bool enabled = true;
    QVariantMap params;

    double num(const QString& key) const;
    bool flag(const QString& key) const;
    QString text(const QString& key) const;
    void set(const QString& key, const QVariant& v) { params.insert(key, v); }
};

class EffectRegistry {
public:
    static const QVector<EffectDef>& all();
    static const EffectDef* find(const QString& type);
    static EffectInstance make(const QString& type);
    static QStringList categories(bool video);
};

}  // namespace hr
