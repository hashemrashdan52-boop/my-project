#pragma once
#include <QHash>
#include <QString>

// The program speaks English or Arabic.  The English text is written in the code itself, TR("English text"), and
// the Arabic one comes from a table (I18nArabic.cpp).  A sentence without an Arabic entry is shown in English, so
// a missing translation is never a crash; the tests check that every TR() text has its Arabic entry.
namespace hr::i18n {

enum class Lang { English, Arabic };

Lang current();
void setCurrent(Lang l);
bool isRtl();                                   // true for Arabic

QString code(Lang l);                           // "en" / "ar"
Lang fromCode(const QString& c, Lang fallback = Lang::English);
QString nativeName(Lang l);                     // "English" / "العربية"

QString tr(const char* english);
QString tr(const QString& english);

// The Arabic table (English text -> Arabic text) and a lookup for the tests.
const QHash<QString, QString>& arabicCatalog();
bool hasArabic(const QString& english);

// Language stored by the installer / the language chooser / the Preferences dialog ("" = never chosen).
QString savedLanguageCode();
void saveLanguageCode(const QString& code);

}  // namespace hr::i18n

#define TR(text) ::hr::i18n::tr(text)
// Marks a text that is stored as plain const char* and translated later with TR(variable).
#define TR_NOOP(text) text
