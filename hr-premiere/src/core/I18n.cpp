#include "I18n.h"

#include <QSettings>
#include <atomic>

namespace hr::i18n {

static std::atomic<int> g_lang{int(Lang::English)};

Lang current() { return Lang(g_lang.load()); }
void setCurrent(Lang l) { g_lang = int(l); }
bool isRtl() { return current() == Lang::Arabic; }

QString code(Lang l) { return l == Lang::Arabic ? QStringLiteral("ar") : QStringLiteral("en"); }

Lang fromCode(const QString& c, Lang fallback) {
    const QString k = c.trimmed().toLower();
    if (k == "ar" || k.startsWith("ar-") || k.startsWith("ar_") || k == "arabic") return Lang::Arabic;
    if (k == "en" || k.startsWith("en-") || k.startsWith("en_") || k == "english") return Lang::English;
    return fallback;
}

QString nativeName(Lang l) { return l == Lang::Arabic ? QString::fromUtf8("العربية") : QStringLiteral("English"); }

QString tr(const QString& english) {
    if (current() != Lang::Arabic || english.isEmpty()) return english;
    const auto& cat = arabicCatalog();
    auto it = cat.constFind(english);
    return it == cat.constEnd() ? english : it.value();
}

QString tr(const char* english) { return tr(QString::fromUtf8(english)); }

bool hasArabic(const QString& english) { return arabicCatalog().contains(english); }

QString savedLanguageCode() { return QSettings().value("language").toString(); }
void saveLanguageCode(const QString& c) {
    QSettings s;
    s.setValue("language", c);
    s.sync();
}

}  // namespace hr::i18n
