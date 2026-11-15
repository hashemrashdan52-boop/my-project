#include "core/I18n.h"
#include "Project.h"
#include "TextUtil.h"

#include "media/MediaProbe.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>

namespace hr {

// ------------------------------------------------------------------ undo command
TimelineCommand::TimelineCommand(Project* p, Timeline before, Timeline after, const QString& text,
                                 const QString& mergeKey)
    : m_project(p), m_before(std::move(before)), m_after(std::move(after)), m_mergeKey(mergeKey) {
    setText(text);
}
void TimelineCommand::undo() { m_project->applyTimeline(m_before); }
void TimelineCommand::redo() { m_project->applyTimeline(m_after); }
bool TimelineCommand::mergeWith(const QUndoCommand* other) {
    if (other->id() != id() || m_mergeKey.isEmpty()) return false;
    auto* o = static_cast<const TimelineCommand*>(other);
    if (o->m_mergeKey != m_mergeKey) return false;
    m_after = o->m_after;
    return true;
}

// ------------------------------------------------------------------ project
Project::Project(QObject* parent) : QObject(parent) { m_undo.setUndoLimit(200); }

QString Project::name() const {
    return m_filePath.isEmpty() ? TR("Untitled") : QFileInfo(m_filePath).completeBaseName();
}

const MediaItem* Project::mediaById(const QString& id) const {
    for (const auto& m : m_media) if (m.id == id) return &m;
    return nullptr;
}
MediaItem* Project::mediaById(const QString& id) {
    for (auto& m : m_media) if (m.id == id) return &m;
    return nullptr;
}

std::shared_ptr<const Timeline> Project::snapshot() {
    if (!m_snapshot) m_snapshot = std::make_shared<const Timeline>(m_timeline);
    return m_snapshot;
}

QString Project::importFile(const QString& path, QString* error) {
    // the same file is only imported once
    for (const auto& m : m_media)
        if (QFileInfo(m.path) == QFileInfo(path)) return m.id;
    MediaItem item;
    item.id = newId();
    item.path = QFileInfo(path).absoluteFilePath();
    item.name = QFileInfo(path).fileName();
    if (!probeMedia(item.path, item, error)) return {};
    m_media.push_back(item);
    setDirty(true);
    emit mediaAdded(item.id);
    return item.id;
}

void Project::removeMedia(const QString& id) {
    for (int i = 0; i < m_media.size(); ++i)
        if (m_media[i].id == id) {
            m_media.removeAt(i);
            setDirty(true);
            emit mediaRemoved(id);
            return;
        }
}

void Project::setMediaMarks(const QString& id, double in, double out) {
    if (MediaItem* m = mediaById(id)) {
        m->markIn = in;
        m->markOut = out;
        emit mediaChanged(id);
    }
}

void Project::setTranscript(const QString& id, const QVector<TranscriptSegment>& segments, const QString& language, const QString& model) {
    if (MediaItem* m = mediaById(id)) {
        m->transcript = segments;
        m->transcriptLanguage = language;
        m->transcriptModel = model;
        setDirty(true);
        emit mediaChanged(id);
    }
}

void Project::setTranscriptSegments(const QString& id, const QVector<TranscriptSegment>& segments) {
    if (MediaItem* m = mediaById(id)) {
        m->transcript = segments;
        setDirty(true);
        emit mediaChanged(id);
    }
}

TranscriptCommand::TranscriptCommand(Project* p, const QString& mediaId, QVector<TranscriptSegment> before,
                                     QVector<TranscriptSegment> after, const QString& text)
    : m_project(p), m_media(mediaId), m_before(std::move(before)), m_after(std::move(after)) {
    setText(text);
}
void TranscriptCommand::undo() { m_project->setTranscriptSegments(m_media, m_before); }
void TranscriptCommand::redo() { m_project->setTranscriptSegments(m_media, m_after); }

bool Project::editSentence(const QString& mediaId, int index, const QString& text) {
    const MediaItem* m = mediaById(mediaId);
    if (!m || index < 0 || index >= m->transcript.size()) return false;
    const QString clean = text.simplified();
    if (clean.isEmpty() || clean == m->transcript[index].text) return false;
    QVector<TranscriptSegment> after = m->transcript;
    after[index].text = clean;
    after[index].edited = true;
    m_undo.push(new TranscriptCommand(this, mediaId, m->transcript, after, TR("Edit sentence")));
    return true;
}

int Project::replaceInTranscripts(const QString& from, const QString& to) {
    if (from.trimmed().isEmpty()) return 0;
    const QRegularExpression re = tolerantRegex(from);
    QString safeTo = to;
    safeTo.replace('\\', QStringLiteral("\\\\"));   // no back-references in the replacement text
    struct Change { QString id; QVector<TranscriptSegment> before, after; };
    QVector<Change> changes;
    int total = 0;
    for (const MediaItem& m : m_media) {
        Change ch{m.id, m.transcript, m.transcript};
        bool any = false;
        for (TranscriptSegment& sg : ch.after) {
            int hits = 0;
            auto it = re.globalMatch(sg.text);
            while (it.hasNext()) { it.next(); ++hits; }
            if (!hits) continue;
            sg.text.replace(re, safeTo);
            sg.edited = true;
            total += hits;
            any = true;
        }
        if (any) changes.push_back(ch);
    }
    if (changes.isEmpty()) return 0;
    m_undo.beginMacro(TR("Replace word"));
    for (const Change& ch : changes) m_undo.push(new TranscriptCommand(this, ch.id, ch.before, ch.after, TR("Replace word")));
    m_undo.endMacro();
    return total;
}

void Project::setSelection(const QSet<QString>& ids) {
    if (ids == m_selection) return;
    m_selection = ids;
    emit selectionChanged();
}

QString Project::primarySelection() const {
    // the earliest selected clip is the one shown in Effect Controls
    QString best;
    double bestStart = 1e18;
    for (const QString& id : m_selection)
        if (const Clip* c = m_timeline.clip(id))
            if (c->start < bestStart) { bestStart = c->start; best = id; }
    return best;
}

void Project::setDirty(bool d) {
    if (m_dirty == d) return;
    m_dirty = d;
    emit dirtyChanged(d);
}

void Project::applyTimeline(const Timeline& t, bool markDirty) {
    m_timeline = t;
    m_snapshot.reset();
    // drop selection entries whose clips no longer exist
    QSet<QString> keep;
    for (const QString& id : m_selection) if (m_timeline.find(id).valid()) keep.insert(id);
    const bool selChanged = keep != m_selection;
    m_selection = keep;
    if (markDirty) setDirty(true);
    emit timelineChanged();
    if (selChanged) emit selectionChanged();
}

void Project::edit(const QString& text, const std::function<void(Timeline&)>& fn, const QString& mergeKey) {
    Timeline before = m_timeline;
    Timeline after = m_timeline;
    fn(after);
    m_undo.push(new TimelineCommand(this, before, after, text, mergeKey));
}

void Project::setTimelineLive(const Timeline& t) {
    m_timeline = t;
    m_snapshot.reset();
    emit timelineChanged();
}

void Project::commit(const QString& text, const Timeline& before) {
    Timeline after = m_timeline;
    m_timeline = before;  // the command's first redo() re-applies `after`
    m_undo.push(new TimelineCommand(this, before, after, text, QString()));
}

void Project::setSequenceSettings(int w, int h, double fps) {
    edit(TR("Sequence Settings"), [&](Timeline& t) {
        t.width = w;
        t.height = h;
        t.fps = fps;
    });
}

void Project::clear() {
    m_media.clear();
    m_timeline = Timeline::makeDefault();
    m_snapshot.reset();
    m_selection.clear();
    m_undo.clear();
    m_filePath.clear();
    setDirty(false);
    emit projectReset();
    emit timelineChanged();
    emit selectionChanged();
    emit titleChanged();
}

// ------------------------------------------------------------------ JSON
static QJsonObject effectToJson(const EffectInstance& e) {
    QJsonObject o;
    o["type"] = e.type;
    o["enabled"] = e.enabled;
    o["params"] = QJsonObject::fromVariantMap(e.params);
    return o;
}
static EffectInstance effectFromJson(const QJsonObject& o) {
    EffectInstance e = EffectRegistry::make(o["type"].toString());
    e.enabled = o["enabled"].toBool(true);
    const QVariantMap m = o["params"].toObject().toVariantMap();
    for (auto it = m.begin(); it != m.end(); ++it) e.params.insert(it.key(), it.value());
    return e;
}
static QJsonObject clipToJson(const Clip& c) {
    QJsonObject o;
    o["id"] = c.id;
    o["kind"] = c.kind == ClipKind::Title ? "title" : "media";
    o["mediaId"] = c.mediaId;
    o["path"] = c.path;
    o["mediaKind"] = int(c.mediaKind);
    o["name"] = c.name;
    o["linkId"] = c.linkId;
    o["start"] = c.start;
    o["duration"] = c.duration;
    o["srcIn"] = c.srcIn;
    o["sourceLength"] = c.sourceLength;
    o["speed"] = c.speed;
    o["enabled"] = c.enabled;
    if (!c.role.isEmpty()) o["role"] = c.role;
    QJsonArray fx;
    for (const auto& e : c.effects) fx.append(effectToJson(e));
    o["effects"] = fx;
    return o;
}
static Clip clipFromJson(const QJsonObject& o) {
    Clip c;
    c.id = o["id"].toString();
    c.kind = o["kind"].toString() == "title" ? ClipKind::Title : ClipKind::Media;
    c.mediaId = o["mediaId"].toString();
    c.path = o["path"].toString();
    c.mediaKind = MediaKind(o["mediaKind"].toInt());
    c.name = o["name"].toString();
    c.linkId = o["linkId"].toString();
    c.start = o["start"].toDouble();
    c.duration = o["duration"].toDouble();
    c.srcIn = o["srcIn"].toDouble();
    c.sourceLength = o["sourceLength"].toDouble();
    c.speed = o["speed"].toDouble(1.0);
    if (c.speed <= 0) c.speed = 1.0;
    c.enabled = o["enabled"].toBool(true);
    c.role = o["role"].toString();
    for (const auto& v : o["effects"].toArray()) c.effects.push_back(effectFromJson(v.toObject()));
    return c;
}
static QJsonObject trackToJson(const Track& t) {
    QJsonObject o;
    o["name"] = t.name;
    o["enabled"] = t.enabled;
    o["locked"] = t.locked;
    o["solo"] = t.solo;
    QJsonArray cl;
    for (const auto& c : t.clips) cl.append(clipToJson(c));
    o["clips"] = cl;
    return o;
}
static Track trackFromJson(const QJsonObject& o, TrackType type) {
    Track t;
    t.type = type;
    t.name = o["name"].toString();
    t.enabled = o["enabled"].toBool(true);
    t.locked = o["locked"].toBool(false);
    t.solo = o["solo"].toBool(false);
    for (const auto& v : o["clips"].toArray()) t.clips.push_back(clipFromJson(v.toObject()));
    return t;
}

bool Project::save(const QString& path, QString* error) {
    QJsonObject root;
    root["format"] = "hr-premiere-project";
    root["version"] = 1;
    QJsonArray media;
    for (const auto& m : m_media) {
        QJsonObject o;
        o["id"] = m.id;
        o["name"] = m.name;
        o["path"] = m.path;
        o["relPath"] = QDir(QFileInfo(path).absolutePath()).relativeFilePath(m.path);
        o["kind"] = int(m.kind);
        o["duration"] = m.duration;
        o["w"] = m.width;
        o["h"] = m.height;
        o["fps"] = m.fps;
        o["hasVideo"] = m.hasVideo;
        o["hasAudio"] = m.hasAudio;
        o["sampleRate"] = m.sampleRate;
        o["channels"] = m.channels;
        o["markIn"] = m.markIn;
        o["markOut"] = m.markOut;
        if (!m.transcript.isEmpty()) {
            QJsonArray tr;
            for (const TranscriptSegment& sg : m.transcript) {
                QJsonObject so;
                so["s"] = sg.start;
                so["e"] = sg.end;
                so["t"] = sg.text;
                if (sg.edited) so["x"] = true;
                tr.append(so);
            }
            o["transcript"] = tr;
            o["transcriptLanguage"] = m.transcriptLanguage;
            o["transcriptModel"] = m.transcriptModel;
        }
        media.append(o);
    }
    root["media"] = media;

    QJsonObject seq;
    seq["name"] = m_timeline.name;
    seq["width"] = m_timeline.width;
    seq["height"] = m_timeline.height;
    seq["fps"] = m_timeline.fps;
    seq["sampleRate"] = m_timeline.sampleRate;
    seq["markIn"] = m_timeline.markIn;
    seq["markOut"] = m_timeline.markOut;
    QJsonArray v, a, mk;
    for (const auto& t : m_timeline.video) v.append(trackToJson(t));
    for (const auto& t : m_timeline.audio) a.append(trackToJson(t));
    for (const auto& m : m_timeline.markers) {
        QJsonObject o;
        o["time"] = m.time;
        o["name"] = m.name;
        o["color"] = m.color.name();
        mk.append(o);
    }
    seq["video"] = v;
    seq["audio"] = a;
    seq["markers"] = mk;
    root["sequence"] = seq;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    m_filePath = path;
    m_undo.setClean();
    setDirty(false);
    emit titleChanged();
    return true;
}

bool Project::load(const QString& path, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (doc.isNull() || !doc.isObject() || doc.object()["format"].toString() != "hr-premiere-project") {
        if (error) *error = TR("Not an HR Premiere project file");
        return false;
    }
    const QJsonObject root = doc.object();
    const QDir base(QFileInfo(path).absolutePath());

    m_media.clear();
    m_selection.clear();
    QHash<QString, QString> fixedPaths;  // old path -> resolved path
    for (const auto& vv : root["media"].toArray()) {
        const QJsonObject o = vv.toObject();
        MediaItem m;
        m.id = o["id"].toString();
        m.name = o["name"].toString();
        m.path = o["path"].toString();
        if (!QFileInfo::exists(m.path)) {
            const QString rel = base.absoluteFilePath(o["relPath"].toString());
            if (QFileInfo::exists(rel)) m.path = rel;
        }
        fixedPaths.insert(o["path"].toString(), m.path);
        m.kind = MediaKind(o["kind"].toInt());
        m.duration = o["duration"].toDouble();
        m.width = o["w"].toInt();
        m.height = o["h"].toInt();
        m.fps = o["fps"].toDouble();
        m.hasVideo = o["hasVideo"].toBool();
        m.hasAudio = o["hasAudio"].toBool();
        m.sampleRate = o["sampleRate"].toInt();
        m.channels = o["channels"].toInt();
        m.markIn = o["markIn"].toDouble(-1);
        m.markOut = o["markOut"].toDouble(-1);
        m.offline = !QFileInfo::exists(m.path);
        for (const auto& tv : o["transcript"].toArray()) {
            const QJsonObject so = tv.toObject();
            m.transcript.push_back({so["s"].toDouble(), so["e"].toDouble(), so["t"].toString(), so["x"].toBool(false)});
        }
        m.transcriptLanguage = o["transcriptLanguage"].toString();
        m.transcriptModel = o["transcriptModel"].toString();
        m_media.push_back(m);
    }

    Timeline t;
    const QJsonObject seq = root["sequence"].toObject();
    t.name = seq["name"].toString("Sequence 01");
    t.width = seq["width"].toInt(1920);
    t.height = seq["height"].toInt(1080);
    t.fps = seq["fps"].toDouble(30);
    t.sampleRate = seq["sampleRate"].toInt(48000);
    t.markIn = seq["markIn"].toDouble(-1);
    t.markOut = seq["markOut"].toDouble(-1);
    for (const auto& v : seq["video"].toArray()) t.video.push_back(trackFromJson(v.toObject(), TrackType::Video));
    for (const auto& v : seq["audio"].toArray()) t.audio.push_back(trackFromJson(v.toObject(), TrackType::Audio));
    if (t.video.isEmpty()) t = Timeline::makeDefault();
    for (const auto& v : seq["markers"].toArray()) {
        const QJsonObject o = v.toObject();
        Marker mk;
        mk.time = o["time"].toDouble();
        mk.name = o["name"].toString();
        mk.color = QColor(o["color"].toString("#7bd88f"));
        t.markers.push_back(mk);
    }
    for (auto* tracks : {&t.video, &t.audio})
        for (Track& tr : *tracks)
            for (Clip& c : tr.clips)
                if (fixedPaths.contains(c.path)) c.path = fixedPaths.value(c.path);

    m_undo.clear();
    m_timeline = t;
    m_snapshot.reset();
    m_filePath = path;
    setDirty(false);
    emit projectReset();
    emit timelineChanged();
    emit selectionChanged();
    emit titleChanged();
    for (const auto& m : m_media) emit mediaAdded(m.id);
    return true;
}

}  // namespace hr
