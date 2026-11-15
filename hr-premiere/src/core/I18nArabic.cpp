#include "I18n.h"

namespace hr::i18n {

struct Pair { const char* en; const char* ar; };

// English text -> Arabic text.  Written with tools/ar_add.py; tests/test_ai.cpp checks that every TR("...") of the
// sources has an entry here.
static const Pair kPairs[] = {
#include "ar/menus.inc"
#include "ar/main_window.inc"
#include "ar/dialogs.inc"
#include "ar/panels.inc"
#include "ar/effects.inc"
#include "ar/engine.inc"
#include "ar/ai_panel.inc"
#include "ar/ai_chat.inc"
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
