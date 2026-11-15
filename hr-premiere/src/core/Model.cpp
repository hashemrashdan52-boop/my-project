#include "Model.h"

#include <QUuid>

namespace hr {

QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces).left(12); }

EffectInstance* Clip::effect(const QString& type) {
    for (auto& e : effects)
        if (e.type == type) return &e;
    return nullptr;
}
const EffectInstance* Clip::effect(const QString& type) const {
    for (const auto& e : effects)
        if (e.type == type) return &e;
    return nullptr;
}
bool Clip::hasCustomEffects() const {
    for (const auto& e : effects) {
        const EffectDef* d = EffectRegistry::find(e.type);
        if (d && !d->intrinsic) return true;
    }
    return false;
}

Timeline Timeline::makeDefault(int videoTracks, int audioTracks) {
    Timeline t;
    for (int i = 0; i < videoTracks; ++i) {
        Track tr;
        tr.type = TrackType::Video;
        tr.name = QString("V%1").arg(i + 1);
        t.video.push_back(tr);
    }
    for (int i = 0; i < audioTracks; ++i) {
        Track tr;
        tr.type = TrackType::Audio;
        tr.name = QString("A%1").arg(i + 1);
        t.audio.push_back(tr);
    }
    return t;
}

double Timeline::duration() const {
    double d = 0;
    for (const auto& tr : video)
        if (!tr.clips.isEmpty()) d = std::max(d, tr.clips.last().end());
    for (const auto& tr : audio)
        if (!tr.clips.isEmpty()) d = std::max(d, tr.clips.last().end());
    return d;
}

bool Timeline::isEmpty() const {
    for (const auto& tr : video) if (!tr.clips.isEmpty()) return false;
    for (const auto& tr : audio) if (!tr.clips.isEmpty()) return false;
    return true;
}

ClipRef Timeline::find(const QString& id) const {
    for (int t = 0; t < video.size(); ++t)
        for (int i = 0; i < video[t].clips.size(); ++i)
            if (video[t].clips[i].id == id) return {TrackType::Video, t, i};
    for (int t = 0; t < audio.size(); ++t)
        for (int i = 0; i < audio[t].clips.size(); ++i)
            if (audio[t].clips[i].id == id) return {TrackType::Audio, t, i};
    return {};
}

Clip* Timeline::clip(const ClipRef& r) {
    if (!r.valid()) return nullptr;
    auto& tr = tracks(r.type);
    if (r.track >= tr.size() || r.index >= tr[r.track].clips.size()) return nullptr;
    return &tr[r.track].clips[r.index];
}
const Clip* Timeline::clip(const ClipRef& r) const {
    return const_cast<Timeline*>(this)->clip(r);
}
Clip* Timeline::clip(const QString& id) { return clip(find(id)); }
const Clip* Timeline::clip(const QString& id) const { return clip(find(id)); }

}  // namespace hr
