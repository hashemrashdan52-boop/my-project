#pragma once
#include <QRegularExpression>
#include <QString>

namespace hr {

// Text helpers for matching Arabic (and Latin) words the way people actually type them.

// For comparing / keyword matching: lower case, no diacritics or tatweel, the alef / ya / ta-marbuta / waw-hamza
// variants folded together, Arabic-Indic digits turned into 0-9, Arabic punctuation turned into spaces.
QString normalizeArabic(const QString& s);

// A regular expression that finds `literal` ignoring case, diacritics and the usual spelling variants
// (أ إ آ ا, ي ى, ه ة ...). Spaces match any run of white space.
QRegularExpression tolerantRegex(const QString& literal);

bool containsTolerant(const QString& text, const QString& query);

// "1,234" style helpers are not needed; this only converts digits.
QString westernDigits(const QString& s);

}  // namespace hr
