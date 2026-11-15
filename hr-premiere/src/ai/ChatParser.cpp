#include "ChatParser.h"

#include "core/TextUtil.h"

#include <QHash>
#include <QRegularExpression>
#include <cmath>

namespace hr::ai {

namespace {

// ------------------------------------------------------------------------------------------------ words
// A text broken into words, normalised for matching (see normalizeArabic) and without the Arabic article.
struct Words {
    QString raw;
    QStringList rawTokens;      // as typed (for texts that are copied: replacement words, new sentences)
    QStringList tokens;         // normalised
    QStringList cores;          // normalised, without ال / وال / بال / لل ...
    QStringList alts;           // normalised, without a leading "و" (and): "وكبيرة" -> "كبيرة"
    QString joined;             // the normalised tokens, one space apart

    explicit Words(const QString& text) : raw(text) {
        rawTokens = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        for (const QString& t : rawTokens) {
            const QString n = normalizeArabic(t).simplified();
            if (n.isEmpty()) continue;
            tokens << n;
            cores << stripArticle(n);
            alts << ((n.size() >= 4 && n.startsWith(QChar(0x0648)) && !n.startsWith(QString::fromUtf8("وال"))) ? stripArticle(n.mid(1)) : n);
        }
        joined = tokens.join(' ');
    }

    static QString stripArticle(const QString& w) {
        static const QString al = QString::fromUtf8("ال");
        static const QString lal = QString::fromUtf8("لل");
        for (const QString& p : {QString::fromUtf8("وال"), QString::fromUtf8("بال"), QString::fromUtf8("فال"), QString::fromUtf8("كال"), al})
            if (w.startsWith(p) && w.size() - p.size() >= 2) return w.mid(p.size());
        if (w.startsWith(lal) && w.size() - lal.size() >= 2) return w.mid(lal.size());
        return w;
    }

    // a word that starts with one of the stems (the words of English and Arabic are given as stems)
    bool any(const QStringList& stems) const {
        for (const QString& s : stems)
            if (s.contains(' ')) {                                // several words: must appear as a phrase
                if (joined.contains(s)) return true;
            } else {
                for (int i = 0; i < tokens.size(); ++i)
                    if (tokens[i].startsWith(s) || cores[i].startsWith(s) || alts[i].startsWith(s)) return true;
            }
        return false;
    }
    // a word that is exactly one of the given words
    bool anyExact(const QStringList& words) const {
        for (int i = 0; i < tokens.size(); ++i)
            for (const QString& w : words)
                if (tokens[i] == w || cores[i] == w || alts[i] == w) return true;
        return false;
    }
    bool phrase(const QString& p) const { return joined.contains(normalizeArabic(p).simplified()); }
    int indexOfExact(const QStringList& words, int from = 0) const {
        for (int i = from; i < tokens.size(); ++i)
            for (const QString& w : words)
                if (tokens[i] == w || cores[i] == w) return i;
        return -1;
    }
    int indexOfStem(const QStringList& stems, int from = 0) const {
        for (int i = from; i < tokens.size(); ++i)
            for (const QString& s : stems)
                if (tokens[i].startsWith(s) || cores[i].startsWith(s)) return i;
        return -1;
    }
};

QStringList L(std::initializer_list<const char*> l) {
    QStringList out;
    for (const char* s : l) out << normalizeArabic(QString::fromUtf8(s)).simplified();
    return out;
}

// ------------------------------------------------------------------------------------------------ vocabulary
const QStringList kRemove = L({"شيل", "احذف", "حذف", "امسح", "ازل", "ازيل", "ابعد", "نضف", "نظف", "الغ", "طفي", "اخفي", "بدون", "remove", "delete", "hide",
                               "without", "drop", "cut out", "get rid", "من غير", "ما بدي", "مش بدي"});
bool removing(const Words& w) { return w.any(kRemove) || w.anyExact(L({"no", "off", "not", "نو"})); }
const QStringList kAdd = L({"ضيف", "اضف", "اضافه", "زيد", "حط", "ركب", "اعمل", "سوي", "انشئ", "ولد", "فعل", "شغل", "اظهر", "ابغى", "بدي", "add", "put",
                            "insert", "make", "create", "show", "enable", "turn on", "use"});
const QStringList kLess = L({"اقل", "خفف", "قلل", "هادي", "هادئ", "قليل", "نقص", "less", "softer", "reduce", "lower", "calmer", "fewer", "subtle"});
const QStringList kMore = L({"اكثر", "زيد", "قوي", "اقوى", "كتير", "اكبر", "اشد", "more", "stronger", "increase", "bigger", "extra"});
const QStringList kChange = L({"غير", "بدل", "استبدل", "عدل", "صحح", "صلح", "اصلح", "اعدل", "اصحح", "اغير", "ابدل", "replace", "change", "correct", "fix", "rename"});
const QStringList kSilence = L({"صمت", "سكوت", "ساكت", "سكت", "فراغ", "وقفات", "وقفه", "هدوء", "silence", "silent", "pause", "dead air"});
const QStringList kCaption = L({"ترجم", "سبتايتل", "سب تايتل", "subtitle", "caption", "كابشن", "كبشن", "نصوص", "تعليق مكتوب", "closed caption"});
const QStringList kMotion = L({"زوم", "zoom", "تقريب", "حركه", "حركات", "تحريك", "موشن", "motion", "ايفكت", "افكت", "effect", "تاثير", "كين بيرنز", "punch", "movement", "movements"});
const QStringList kFade = L({"fade", "تلاشي", "تدريج"});
bool fading(const Words& w) { return w.any(kFade) || w.anyExact(L({"فيد", "فيدات", "فيدر", "فيدين"})); }
const QStringList kSentenceNoun = L({"جمله", "جمل", "سطر", "عباره", "sentence", "line"});

const QHash<QString, QString>& colors() {
    static const QHash<QString, QString> c = [] {
        QHash<QString, QString> m;
        auto add = [&](std::initializer_list<const char*> names, const char* hex) {
            for (const char* n : names) m.insert(normalizeArabic(QString::fromUtf8(n)).simplified(), hex);
        };
        add({"ابيض", "بيضاء", "white"}, "#ffffff");
        add({"اسود", "سوداء", "black"}, "#000000");
        add({"احمر", "حمراء", "red"}, "#ff3b30");
        add({"اخضر", "خضراء", "green"}, "#34c759");
        add({"ازرق", "زرقاء", "blue"}, "#0a84ff");
        add({"اصفر", "صفراء", "yellow"}, "#ffd60a");
        add({"برتقالي", "برتقاليه", "orange"}, "#ff9f0a");
        add({"بنفسجي", "بنفسجيه", "موف", "purple", "violet"}, "#bf5af2");
        add({"وردي", "وردية", "زهري", "pink"}, "#ff6bd6");
        add({"رمادي", "رماديه", "gray", "grey"}, "#8e8e93");
        add({"ذهبي", "ذهبيه", "gold", "golden"}, "#ffd700");
        add({"سماوي", "تركوازي", "cyan", "turquoise"}, "#5ac8fa");
        add({"بني", "brown"}, "#a2845e");
        return m;
    }();
    return c;
}

// ------------------------------------------------------------------------------------------------ numbers
const QHash<QString, double>& numberWords() {
    static const QHash<QString, double> m = [] {
        QHash<QString, double> h;
        auto add = [&](std::initializer_list<const char*> names, double v) {
            for (const char* n : names) h.insert(normalizeArabic(QString::fromUtf8(n)).simplified(), v);
        };
        add({"واحد", "واحده", "one"}, 1);
        add({"اثنين", "اثنان", "ثنتين", "اثنتين", "two"}, 2);
        add({"ثلاث", "ثلاثه", "three"}, 3);
        add({"اربع", "اربعه", "four"}, 4);
        add({"خمس", "خمسه", "five"}, 5);
        add({"ست", "سته", "six"}, 6);
        add({"سبع", "سبعه", "seven"}, 7);
        add({"ثمان", "ثمانيه", "eight"}, 8);
        add({"تسع", "تسعه", "nine"}, 9);
        add({"عشر", "عشره", "ten"}, 10);
        return h;
    }();
    return m;
}

const QHash<QString, int>& ordinals() {
    static const QHash<QString, int> m = [] {
        QHash<QString, int> h;
        auto add = [&](std::initializer_list<const char*> names, int v) {
            for (const char* n : names) h.insert(normalizeArabic(QString::fromUtf8(n)).simplified(), v);
        };
        add({"الاولي", "الاول", "اولي", "اول", "first"}, 1);
        add({"الثانيه", "الثاني", "ثانيه", "second"}, 2);
        add({"الثالثه", "الثالث", "ثالثه", "third"}, 3);
        add({"الرابعه", "الرابع", "fourth"}, 4);
        add({"الخامسه", "الخامس", "fifth"}, 5);
        add({"السادسه", "السادس", "sixth"}, 6);
        add({"السابعه", "السابع", "seventh"}, 7);
        add({"الثامنه", "الثامن", "eighth"}, 8);
        add({"التاسعه", "التاسع", "ninth"}, 9);
        add({"العاشره", "العاشر", "tenth"}, 10);
        return h;
    }();
    return m;
}

bool toNumber(const QString& tok, double* out) {
    bool ok = false;
    const double v = QString(tok).replace(',', '.').toDouble(&ok);
    if (ok) { *out = v; return true; }
    const auto it = numberWords().constFind(tok);
    if (it != numberWords().constEnd()) { *out = it.value(); return true; }
    return false;
}

// "half a second", "1.5 s", "two seconds", "ثانيتين" ... in seconds; NaN when there is no length of time
double findSeconds(const Words& w) {
    static const QStringList unitStems = L({"ثانيه", "ثواني", "ثوان", "ثانيتين", "sec", "second"});
    for (int i = 0; i < w.tokens.size(); ++i) {
        const QString& t = w.tokens[i];
        // "0.5s" / "2sec" glued together
        static const QRegularExpression glued("^(\\d+(?:[.,]\\d+)?)(s|sec|secs|seconds?|ث)$");
        const auto gm = glued.match(t);
        if (gm.hasMatch()) return QString(gm.captured(1)).replace(',', '.').toDouble();
        bool isUnit = false;
        for (const QString& u : unitStems)
            if (t.startsWith(u) || w.cores[i].startsWith(u)) isUnit = true;
        if (t == "ث" || t == "s") isUnit = true;
        if (!isUnit) continue;
        if (t.startsWith(normalizeArabic(QString::fromUtf8("ثانيتين")))) return 2;            // "two seconds" in one word
        double n = 0;
        if (i > 0) {
            const QString& p = w.tokens[i - 1];
            static const QStringList half = L({"نص", "نصف", "half"});
            static const QStringList quarter = L({"ربع", "quarter"});
            if (half.contains(p)) return 0.5;
            if (quarter.contains(p)) return 0.25;
            if (toNumber(p, &n)) {
                if (i + 1 < w.tokens.size() && half.contains(w.tokens[i + 1])) return n + 0.5;
                return n;
            }
        }
        if (i + 1 < w.tokens.size()) {                                                         // "ثانية ونص"
            const QString& nx = w.tokens[i + 1];
            if (nx == normalizeArabic(QString::fromUtf8("ونص")) || nx == normalizeArabic(QString::fromUtf8("ونصف"))) return 1.5;
        }
        return 1;                                                                               // "a second"
    }
    return qQNaN();
}

QString findColor(const Words& w) {
    for (int i = 0; i < w.tokens.size(); ++i) {
        auto it = colors().constFind(w.tokens[i]);
        if (it != colors().constEnd()) return it.value();
        it = colors().constFind(w.cores[i]);
        if (it != colors().constEnd()) return it.value();
    }
    return {};
}

QString findPosition(const Words& w, bool captionContext) {
    static const QStringList top = L({"فوق", "اعلي", "top", "upper"});
    static const QStringList bottom = L({"تحت", "اسفل", "bottom", "lower"});
    static const QStringList center = L({"وسط", "منتصف", "center", "centre", "middle"});
    if (w.anyExact(top) || w.any(L({"اعلي"}))) return "top";
    if (w.anyExact(bottom) || w.any(L({"اسفل"}))) return "bottom";
    if (w.anyExact(center)) return "center";
    if (captionContext && w.anyExact(L({"نص"})) && !w.phrase("نص ثانيه")) return "center";
    return {};
}

// the texts inside quotation marks
QStringList quoted(const QString& raw) {
    QStringList out;
    static const QRegularExpression q(QString::fromUtf8("[\"“”«»‘’'`]([^\"“”«»‘’'`]+)[\"“”«»‘’'`]"));
    auto it = q.globalMatch(raw);
    while (it.hasNext()) out << it.next().captured(1).trimmed();
    return out;
}

QString clean(QString s) {
    s = s.trimmed();
    static const QString trim = QString::fromUtf8("\"“”«»‘’'`.,،؟?!:؛ ");
    while (!s.isEmpty() && trim.contains(s.front())) s.remove(0, 1);
    while (!s.isEmpty() && trim.contains(s.back())) s.chop(1);
    return s;
}

int sentenceReference(const Words& w) {
    for (int i = 0; i < w.tokens.size(); ++i) {
        bool isNoun = false;
        for (const QString& n : kSentenceNoun)
            if (w.tokens[i].startsWith(n) || w.cores[i].startsWith(n)) isNoun = true;
        if (!isNoun) continue;
        // "اخر جمله" / "جمله 3" / "الجمله الثالثه" / "sentence 3" / "the first sentence"
        for (int j : {i - 1, i + 1}) {
            if (j < 0 || j >= w.tokens.size()) continue;
            double n = 0;
            if (toNumber(w.tokens[j], &n) && n >= 1) return int(n);
            const auto o = ordinals().constFind(w.tokens[j]);
            if (o != ordinals().constEnd()) return o.value();
            if (w.cores[j].startsWith(normalizeArabic(QString::fromUtf8("اخر"))) || w.tokens[j] == "last") return -1;
        }
        if (i + 2 < w.tokens.size()) {                                       // "جمله رقم 3"
            double n = 0;
            if (w.cores[i + 1].startsWith(normalizeArabic(QString::fromUtf8("رقم"))) && toNumber(w.tokens[i + 2], &n) && n >= 1) return int(n);
        }
    }
    return 0;
}

// the text after a word such as "to" / "الى" / ":" in the raw words (for new sentences and replacement words)
QString textAfterConnector(const Words& w, int fromToken, int* connectorIndex = nullptr) {
    static const QStringList connectors = L({"الي", "ب", "بدل", "مكان", "to", "with", "by", "into"});
    for (int i = fromToken; i < w.tokens.size(); ++i)
        if (connectors.contains(w.tokens[i]) || w.rawTokens[i] == ":" || w.rawTokens[i].endsWith(':')) {
            if (connectorIndex) *connectorIndex = i;
            return clean(QStringList(w.rawTokens.mid(i + 1)).join(' '));
        }
    return {};
}

}  // namespace

// ================================================================================================ the rules
Command parseChat(const QString& text) {
    Command c;
    const Words w(text);
    c.normalized = w.joined;
    if (w.tokens.isEmpty()) return c;

    const QStringList q = quoted(text);

    // ---- short answers to a question the assistant asked
    static const QStringList yes = L({"نعم", "ايه", "اه", "اي", "ايوه", "ايوا", "اكيد", "اكد", "تمام", "ماشي", "اوكي", "اوك", "ok", "okay", "yes", "y", "yep", "yeah",
                                      "sure", "يلا", "طبعا", "اجل", "موافق", "كمل", "proceed", "confirm"});
    static const QStringList no = L({"لا", "كلا", "no", "n", "nope", "الغاء", "cancel", "بلاش", "stop", "ابدا", "مش هلا", "لاء"});
    if (w.tokens.size() <= 3) {
        bool allYes = true, allNo = true;
        static const QStringList polite = L({"please", "من فضلك", "فضلك", "thanks", "يا", "هلا", "now", "الان", "ثم"});
        for (int i = 0; i < w.tokens.size(); ++i) {
            const bool isYes = yes.contains(w.tokens[i]) || yes.contains(w.cores[i]);
            const bool isNo = no.contains(w.tokens[i]) || no.contains(w.cores[i]);
            const bool isPolite = polite.contains(w.tokens[i]);
            allYes = allYes && (isYes || isPolite);
            allNo = allNo && (isNo || isPolite);
        }
        if (allYes && w.anyExact(yes)) return c.intent = Intent::Yes, c;
        if (allNo && w.anyExact(no)) return c.intent = Intent::No, c;
    }

    // ---- undo / redo
    static const QStringList undoWords = L({"تراجع", "undo"});
    if (w.any(undoWords)) return c.intent = Intent::Undo, c;
    if (w.anyExact(L({"اعاده", "redo"})) || (w.any(L({"اعد"})) && w.any(L({"اخر", "خطوه"})) && !w.any(kSilence + kCaption))) return c.intent = Intent::Redo, c;
    static const QStringList back = L({"ارجع", "رجع", "رجعني", "رجعلي", "رجعها", "رجعه", "go back"});
    if (w.any(back) && (w.any(L({"قبل", "اخر", "الاول", "الاصل", "الوضع", "كان", "previous", "last", "original"})) || w.tokens.size() <= 2)) return c.intent = Intent::Undo, c;
    if (w.any(L({"الغ"})) && w.any(L({"اخر"}))) return c.intent = Intent::Undo, c;

    // ---- replacing a word: "غيّر كلمة «أ» إلى «ب»", "بدل مرحبا ب اهلا", "replace x with y"
    const int verbAt = w.indexOfStem(kChange);
    if (verbAt >= 0 && sentenceReference(w) == 0) {
        QString from, to;
        if (q.size() >= 2) {
            from = q[0];
            to = q[1];
        } else {
            static const QStringList connectors = L({"الي", "ب", "بدل", "مكان", "to", "with", "by"});
            int start = verbAt + 1;
            static const QStringList filler = L({"the", "word", "words", "phrase", "كلمه", "لفظ", "عباره", "كلمات"});
            while (start < w.tokens.size() - 1 && filler.contains(w.cores[start])) ++start;
            int conn = -1;
            for (int i = start + 1; i < w.tokens.size(); ++i)
                if (connectors.contains(w.tokens[i])) { conn = i; break; }
            if (conn > start && conn + 1 < w.tokens.size()) {
                from = clean(QStringList(w.rawTokens.mid(start, conn - start)).join(' '));
                to = clean(QStringList(w.rawTokens.mid(conn + 1)).join(' '));
            }
        }
        if (!from.isEmpty() && !to.isEmpty() && from != to) {
            c.intent = Intent::ReplaceWord;
            c.from = from;
            c.to = to;
            return c;
        }
    }

    // ---- sentences: edit / delete / keep
    const int sentence = sentenceReference(w);
    static const QStringList setVerbs = L({"خلي", "خليها", "اجعل", "make", "set", "اكتب", "write", "حط"});
    if (sentence != 0 && (w.any(kChange) || (!q.isEmpty() && w.any(setVerbs)))) {
        QString newText = !q.isEmpty() ? q.last() : textAfterConnector(w, std::max(0, verbAt));
        if (newText.isEmpty()) {                                                         // "الجملة 2 خليها ..."
            const int k = w.indexOfStem(L({"خلي", "خليها", "make", "set"}));
            if (k >= 0) newText = clean(QStringList(w.rawTokens.mid(k + 1)).join(' '));
        }
        if (!newText.isEmpty()) {
            c.intent = Intent::EditSentence;
            c.sentence = sentence;
            c.to = newText;
            return c;
        }
        c.intent = Intent::HowToEditText;
        return c;
    }
    static const QStringList containing = L({"فيها", "فيه", "تحتوي", "تحوي", "بتحتوي", "عليها", "contain", "mention", "includ"});
    static const QStringList containingExact = L({"with", "has", "have", "having", "includes"});     // exact: "without" is something else
    auto queryWord = [&]() -> QString {
        if (!q.isEmpty()) return q.first();
        int k = w.indexOfStem(containing);
        if (k < 0) k = w.indexOfExact(containingExact);
        if (k >= 0) {
            int s = k + 1;
            if (s < w.tokens.size() && (w.cores[s].startsWith(normalizeArabic(QString::fromUtf8("كلم"))) || w.tokens[s] == "word" || w.tokens[s] == "the")) ++s;
            if (s < w.tokens.size() && w.tokens[s] == "word") ++s;
            return clean(QStringList(w.rawTokens.mid(s)).join(' '));
        }
        return {};
    };
    const bool onlyWord = w.anyExact(L({"بس", "فقط", "only", "just"}));
    if ((w.any(L({"خلي", "ابقي", "احتفظ", "keep"})) && onlyWord) && (sentence != 0 || !queryWord().isEmpty())) {
        c.intent = Intent::KeepSentences;
        c.sentence = sentence;
        c.from = queryWord();
        return c;
    }
    if (removing(w) && (w.any(kSentenceNoun) || !queryWord().isEmpty()) && !w.any(kSilence + kCaption + kMotion)) {
        c.intent = Intent::DeleteSentences;
        c.sentence = sentence;
        c.from = queryWord();
        return c;
    }

    // ---- is the text right?
    static const QStringList wrong = L({"غلط", "خطا", "اخطاء", "مشكوك", "doubt", "wrong", "mistake", "error", "incorrect", "inaccurate", "غير صحيح", "مو صح", "مش صح", "مش مظبوط", "مو مظبوط", "مش دقيق"});
    static const QStringList textNoun = L({"كلام", "نص", "جمل", "جمله", "تفريغ", "ترجم", "text", "transcript", "sentence", "words", "كلمات"});
    if ((w.any(wrong) || w.phrase("مش صح") || w.phrase("مو صح") || w.phrase("مش مظبوط") || w.phrase("مو مظبوط")) && w.any(textNoun)) return c.intent = Intent::ShowDoubtful, c;
    if ((w.any(kChange) || w.any(L({"edit", "correct", "fix"}))) && w.any(textNoun)) return c.intent = Intent::HowToEditText, c;

    // ---- sound quality
    static const QStringList noisy = L({"ضجيج", "تشويش", "مشوش", "noise", "noisy", "denoise", "hiss", "خشخشه"});
    if (w.any(noisy) || ((w.any(L({"صوت", "الصوت", "audio", "sound"}))) &&
                         (w.any(L({"سيء", "ضعيف", "مش واضح", "مو واضح", "مش كويس", "مو كويس", "مش مزبوط", "unclear", "poor", "bad", "low", "quiet"})) || w.phrase("مش واضح") ||
                          w.phrase("مو واضح") || w.phrase("مش كويس")) &&
                          !w.any(kSilence)))
        return c.intent = Intent::CleanAudio, c.level = "strong", c;
    if (w.any(L({"نظف الصوت", "حسن الصوت", "clean audio", "enhance audio", "improve audio"})) || (w.any(L({"حسن", "نضف", "نظف", "clean", "enhance", "improve"})) && w.any(L({"صوت", "audio", "sound"}))))
        return c.intent = Intent::CleanAudio, c.level = "light", c;

    // ---- a better recognition
    if (w.phrase("دقه اعلي") || w.phrase("اعلي دقه") || w.phrase("اكثر دقه") || w.phrase("ادق") || w.phrase("اعد التفريغ") || w.phrase("فرغ من جديد") ||
        w.phrase("فرغ مره ثانيه") || w.any(L({"re-transcribe", "retranscribe", "accurate", "more accurate", "better recognition", "transcribe again", "redo the transcript"})) || w.phrase("higher accuracy") ||
        w.phrase("transcribe again") || w.phrase("more accurate") || w.phrase("best quality") || w.phrase("افضل دقه"))
        return c.intent = Intent::BetterAccuracy, c;

    // ---- the whole edit
    static const QStringList montage = L({"مونتاج", "montage", "auto edit", "autoedit", "full edit", "تحرير", "حرر", "تلقائي", "اوتو", "احترافي", "edit the video", "edit it", "edit this", "edit my"});
    const bool partial = w.any(kCaption) || w.any(kSilence) || w.any(kMotion) || fading(w);
    const bool wholeEdit = w.any(montage) && !(w.any(kChange) && w.any(textNoun)) &&
                           (!partial || w.any(L({"مونتاج", "montage", "full", "كامل", "شامل", "auto", "تلقائي", "احترافي", "complete"})) );
    if (wholeEdit || w.phrase("رتب الفيديو") || w.phrase("جهز الفيديو") || w.phrase("حسن الفيديو") || w.phrase("خليه احترافي") || w.phrase("make it professional")) {
        c.intent = Intent::FullEdit;
        if (w.any(L({"سريع", "تيك", "tiktok", "ريلز", "reels", "شورت", "shorts", "ديناميك", "dynamic", "fast", "energetic", "حماسي", "سرعه"})) && !w.phrase("بدون سرعه")) c.preset = "fast";
        else if (w.any(L({"هادي", "هادئ", "سينمائي", "cinematic", "calm", "slow", "بطيء", "ناعم", "relaxed", "smooth"}))) c.preset = "calm";
        // "بدون ترجمة", "no zoom", "ما تشيل الصمت" ...
        static const QStringList negations = L({"بدون", "من غير", "مافي", "ما", "لا", "no", "without", "skip", "مش"});
        auto negated = [&](const QStringList& stems) {
            for (int i = 0; i < w.tokens.size(); ++i) {
                bool neg = false;
                for (const QString& n : negations) neg = neg || w.tokens[i] == n || (w.tokens[i].startsWith(normalizeArabic(QString::fromUtf8("و"))) && w.tokens[i].mid(1) == n);
                if (!neg) continue;
                for (int j = i + 1; j <= std::min(int(w.tokens.size()) - 1, i + 3); ++j)
                    for (const QString& s : stems)
                        if (w.tokens[j].startsWith(s) || w.cores[j].startsWith(s)) return true;
            }
            return false;
        };
        if (negated(kCaption + L({"كلام"}))) c.without << "captions";
        if (negated(kMotion)) c.without << "motion";
        if (negated(kFade) || negated(L({"فيد"}))) c.without << "fades";
        if (negated(kSilence + L({"قطع", "قص", "cut"}))) c.without << "silence";
        return c;
    }

    // ---- exporting the subtitles (before "captions": the words overlap)
    if (w.any(L({"صدر", "تصدير", "export", "احفظ", "save"})) && w.any(L({"srt", "ترجم", "subtitle", "caption"}))) return c.intent = Intent::ExportSrt, c;

    // ---- captions
    const bool captionWords = w.any(kCaption) || w.phrase("الكلام على الشاشه") || w.phrase("نص على الشاشه") || w.phrase("كلام على الفيديو") ||
                              w.phrase("on screen text") || w.phrase("text on screen") || w.phrase("write the words") || w.phrase("subtitles");
    if (captionWords) {
        if (removing(w)) return c.intent = Intent::RemoveCaptions, c;
        c.color = findColor(w);
        c.position = findPosition(w, true);
        c.bigger = w.any(L({"كبر", "اكبر", "كبير", "ضخم", "big", "bigger", "larger", "enlarge"})) && !w.any(L({"صغر", "اصغر", "صغير", "small", "smaller"}));
        c.smaller = w.any(L({"صغر", "اصغر", "صغير", "small", "smaller", "decrease"}));
        const bool style = !c.color.isEmpty() || !c.position.isEmpty() || c.bigger || c.smaller;
        static const QStringList explicitAdd = L({"ضيف", "اضف", "اضافه", "حط", "ركب", "انشئ", "ولد", "فعل", "اظهر", "شغل", "add", "insert", "create", "enable", "turn on", "write", "اكتب"});
        c.intent = style && !w.any(explicitAdd) ? Intent::CaptionStyle : Intent::AddCaptions;
        return c;
    }
    if (w.any(L({"اكتب"})) && w.any(L({"كلام", "حكي", "words", "speech"})) && (w.any(L({"شاشه", "فيديو", "screen", "video", "فوق", "تحت"})))) return c.intent = Intent::AddCaptions, c;

    // ---- movement
    if (w.any(kMotion)) {
        if (removing(w) || w.anyExact(L({"stop", "وقف"}))) return c.intent = Intent::MotionOff, c;
        if (w.any(kLess)) return c.intent = Intent::MotionLess, c;
        if (w.any(kMore)) return c.intent = Intent::MotionMore, c;
        return c.intent = Intent::MotionOn, c;
    }
    if (fading(w)) return c.intent = (removing(w) ? Intent::FadesOff : Intent::FadesOn), c;

    // ---- dead air
    if (w.any(kSilence)) {
        c.seconds = findSeconds(w);
        return c.intent = Intent::RemoveSilence, c;
    }

    // ---- speed
    static const QStringList faster = L({"سرع", "اسرع", "faster", "speed up", "fast forward"});
    static const QStringList slower = L({"بطي", "ابطا", "بطء", "slow", "slower", "slow down"});
    if (w.any(faster) || w.any(slower) || (w.any(L({"سرعه", "speed"})) && (w.any(L({"ضعف", "مرتين"})) || w.any(L({"x", "×"}))))) {
        const bool slow = w.any(slower);
        double n = qQNaN();
        for (const QString& t : w.tokens) {
            static const QRegularExpression times("^(?:x|×)?(\\d+(?:[.,]\\d+)?)(?:x|×)?$");
            const auto m = times.match(t);
            if (m.hasMatch()) { n = QString(m.captured(1)).replace(',', '.').toDouble(); break; }
        }
        if (std::isnan(n)) {
            if (w.any(L({"ضعفين"}))) n = 2;
            else if (w.any(L({"ضعف", "مرتين", "double", "twice"}))) n = 2;
            else if (w.any(L({"نص", "نصف", "half"}))) n = 2;
            else {
                for (int i = 0; i < w.tokens.size(); ++i) {
                    double v = 0;
                    if (toNumber(w.tokens[i], &v) && i + 1 < w.tokens.size() && w.cores[i + 1].startsWith(normalizeArabic(QString::fromUtf8("مر")))) { n = v; break; }
                }
            }
        }
        if (std::isnan(n)) n = 2;
        c.intent = Intent::ChangeSpeed;
        c.number = slow ? 1.0 / n : n;
        return c;
    }

    // ---- scenes, the transcript, the summary, files
    if (w.any(L({"مشهد", "مشاهد", "scene", "لقطه", "لقطات", "shots"})) && w.any(L({"قص", "قطع", "علام", "حدد", "كشف", "detect", "cut", "mark", "split", "find", "وزع"}))) {
        c.intent = Intent::Scenes;
        c.cutScenes = w.any(L({"قص", "قطع", "cut", "split"}));
        return c;
    }
    if (w.any(L({"صدر", "تصدير", "export", "احفظ", "save"})) && w.any(L({"srt", "ترجم", "subtitle", "caption", "نص"}))) return c.intent = Intent::ExportSrt, c;
    if (w.any(L({"فرغ", "تفريغ", "افهم", "استمع", "اسمع", "transcribe", "listen", "حول الصوت", "حول الكلام", "speech to text", "recognise", "recognize"})) ||
        w.phrase("حول الكلام الي نص") || w.phrase("اكتب الكلام")) return c.intent = Intent::Transcribe, c;
    static const QStringList info = L({"لخص", "ملخص", "معلومات", "احصائ", "تحليل", "حلل", "كم مدت", "كم صمت", "كم جمل", "شو في", "وش في", "ايش في", "status", "info", "summary", "analyze", "analyse", "stats", "what is in", "tell me about", "how long", "duration"});
    if (w.any(info)) return c.intent = Intent::Summary, c;
    if (w.any(L({"اللغه", "لغه", "language", "بالعربي", "بالانجليزي", "english", "arabic", "عربي", "انجليزي", "عربيه", "انجليزيه"})) &&
        w.any(L({"كلام", "حكي", "فيديو", "صوت", "speech", "spoken", "video", "talk", "audio", "الغه", "language", "بال"}))) {
        c.intent = Intent::SetLanguage;
        c.language = w.any(L({"انجليز", "english"})) ? "en" : (w.any(L({"عرب", "arabic"})) ? "ar" : QString());
        if (!c.language.isEmpty()) return c;
        c.intent = Intent::Unknown;
    }

    // ---- small talk and help
    static const QStringList helpWords = L({"ساعد", "مساعده", "help", "اوامر", "commands", "كيف استخدم", "شو بتعرف", "شو تقدر", "شو بتقدر", "ايش تقدر", "وش تقدر", "شو فيك",
                                            "ماذا تستطيع", "what can you", "how do i", "how to use", "قائمه"});
    if (w.any(helpWords)) return c.intent = Intent::Help, c;
    if (w.any(L({"شكرا", "مشكور", "يعطيك", "تسلم", "thanks", "thank", "thx", "ممتاز", "حلو", "great", "perfect"}))) return c.intent = Intent::Thanks, c;
    if (w.any(L({"مرحبا", "هلا", "اهلا", "سلام", "هاي", "hello", "hi", "صباح", "مساء", "hey"})) || w.joined == "hi") return c.intent = Intent::Greeting, c;
    return c;
}

}  // namespace hr::ai
