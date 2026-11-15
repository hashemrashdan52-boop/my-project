#include "I18n.h"

namespace hr::i18n {

struct Pair { const char* en; const char* ar; };

static const Pair kPairs[] = {
    {"Ready", "جاهز"},
};

const QHash<QString, QString>& arabicCatalog() {
    static const QHash<QString, QString> table = [] {
        QHash<QString, QString> m;
        for (const Pair& p : kPairs) m.insert(QString::fromUtf8(p.en), QString::fromUtf8(p.ar));
        return m;
    }();
    return table;
}

}  // namespace hr::i18n
