#include "TextUtil.h"

namespace hr {

static bool isDiacritic(QChar c) {
    const ushort u = c.unicode();
    return (u >= 0x064B && u <= 0x065F) || u == 0x0670 || u == 0x0640 /*tatweel*/ || (u >= 0x06D6 && u <= 0x06ED);
}

QString westernDigits(const QString& s) {
    QString out = s;
    for (QChar& c : out) {
        const ushort u = c.unicode();
        if (u >= 0x0660 && u <= 0x0669) c = QChar('0' + (u - 0x0660));        // Arabic-Indic
        else if (u >= 0x06F0 && u <= 0x06F9) c = QChar('0' + (u - 0x06F0));   // Persian
    }
    return out;
}

QString normalizeArabic(const QString& s) {
    QString out;
    out.reserve(s.size());
    for (QChar c : westernDigits(s)) {
        if (isDiacritic(c)) continue;
        switch (c.unicode()) {
        case 0x0623: case 0x0625: case 0x0622: case 0x0671: out += QChar(0x0627); break;   // أ إ آ ٱ -> ا
        case 0x0649: case 0x0626: out += QChar(0x064A); break;                            // ى ئ -> ي
        case 0x0629: out += QChar(0x0647); break;                                         // ة -> ه
        case 0x0624: out += QChar(0x0648); break;                                         // ؤ -> و
        case 0x060C: case 0x061F: case 0x061B: case 0x00AB: case 0x00BB:                  // ، ؟ ؛ « »
        case ',': case '?': case '!': case ';': case ':': case '"': case '\'': case '(': case ')': case '[': case ']':
        case 0x201C: case 0x201D: case 0x2018: case 0x2019: case 0x2026:
            out += ' ';
            break;
        default: out += c.toLower();
        }
    }
    return out;
}

QRegularExpression tolerantRegex(const QString& literal) {
    static const QString marks = QStringLiteral("[\\x{064B}-\\x{065F}\\x{0670}\\x{0640}]*");
    QString pat;
    bool pendingSpace = false;
    for (QChar c : literal.trimmed()) {
        if (c.isSpace()) { pendingSpace = true; continue; }
        if (pendingSpace) { pat += QStringLiteral("\\s+"); pendingSpace = false; }
        if (isDiacritic(c)) continue;
        switch (c.unicode()) {
        case 0x0627: case 0x0623: case 0x0625: case 0x0622: case 0x0671: pat += QStringLiteral("[\\x{0627}\\x{0623}\\x{0625}\\x{0622}\\x{0671}]"); break;
        case 0x064A: case 0x0649: case 0x0626: pat += QStringLiteral("[\\x{064A}\\x{0649}\\x{0626}]"); break;
        case 0x0647: case 0x0629: pat += QStringLiteral("[\\x{0647}\\x{0629}]"); break;
        case 0x0648: case 0x0624: pat += QStringLiteral("[\\x{0648}\\x{0624}]"); break;
        default: pat += QRegularExpression::escape(QString(c));
        }
        pat += marks;
    }
    if (pat.isEmpty()) pat = QStringLiteral("(?!)");   // an empty query matches nothing
    return QRegularExpression(pat, QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
}

bool containsTolerant(const QString& text, const QString& query) {
    if (query.trimmed().isEmpty()) return false;
    return tolerantRegex(query).match(text).hasMatch();
}

}  // namespace hr
