#include "Cleanup.h"

namespace hr::ai {

QString cleanupName(Cleanup c) {
    switch (c) {
    case Cleanup::None: return "none";
    case Cleanup::Light: return "light";
    case Cleanup::Strong: return "strong";
    }
    return "none";
}

Cleanup cleanupFromName(const QString& name, Cleanup fallback) {
    const QString k = name.trimmed().toLower();
    if (k == "none" || k == "off") return Cleanup::None;
    if (k == "light") return Cleanup::Light;
    if (k == "strong") return Cleanup::Strong;
    return fallback;
}

QString cleanupFilter(Cleanup c) {
    switch (c) {
    case Cleanup::None: return {};
    // rumble out, then even out the loudness so quiet speech is not lost
    case Cleanup::Light: return "highpass=f=70,dynaudnorm=f=250:g=15:p=0.9:m=12";
    // plus: keep the speech band, take hiss and hum out (the noise level is tracked), then level it
    case Cleanup::Strong: return "highpass=f=90,lowpass=f=7600,afftdn=nr=18:nf=-35:tn=1,dynaudnorm=f=250:g=15:p=0.9:m=12";
    }
    return {};
}

Cleanup chooseCleanup(const AudioStats& s) {
    if (!s.valid) return Cleanup::Light;             // unknown: the gentle one is safe
    if (s.dynamicDb() < 21) return Cleanup::Strong;  // speech hardly louder than the noise (about 20 dB or less)
    if (s.speechDb < -24) return Cleanup::Light;     // a quiet recording
    if (s.dynamicDb() >= 35 && s.speechDb >= -18) return Cleanup::None;   // clean and loud: leave it alone
    return Cleanup::Light;
}

}  // namespace hr::ai
