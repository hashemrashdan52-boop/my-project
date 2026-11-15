#pragma once
#include "EditOps.h"
#include "Model.h"

#include <QObject>
#include <QSet>
#include <QUndoCommand>
#include <QUndoStack>
#include <functional>
#include <memory>

namespace hr {

class Project;

// Undo command that swaps whole-timeline snapshots.  Timelines are cheap to copy
// (implicitly shared containers) and this keeps every edit trivially reversible.
class TimelineCommand : public QUndoCommand {
public:
    TimelineCommand(Project* p, Timeline before, Timeline after, const QString& text, const QString& mergeKey);
    void undo() override;
    void redo() override;
    int id() const override { return m_mergeKey.isEmpty() ? -1 : 4242; }
    bool mergeWith(const QUndoCommand* other) override;

private:
    Project* m_project;
    Timeline m_before, m_after;
    QString m_mergeKey;
};

// Undo command for a change of one media file's transcript (a corrected sentence, a replaced word).
class TranscriptCommand : public QUndoCommand {
public:
    TranscriptCommand(Project* p, const QString& mediaId, QVector<TranscriptSegment> before, QVector<TranscriptSegment> after,
                      const QString& text);
    void undo() override;
    void redo() override;

private:
    Project* m_project;
    QString m_media;
    QVector<TranscriptSegment> m_before, m_after;
};

class Project : public QObject {
    Q_OBJECT
public:
    explicit Project(QObject* parent = nullptr);

    // ---- data ---------------------------------------------------------------
    QString name() const;
    QString filePath() const { return m_filePath; }
    bool isDirty() const { return m_dirty; }
    const QVector<MediaItem>& media() const { return m_media; }
    const MediaItem* mediaById(const QString& id) const;
    MediaItem* mediaById(const QString& id);
    const Timeline& timeline() const { return m_timeline; }
    std::shared_ptr<const Timeline> snapshot();     // cached copy for render threads
    QUndoStack* undoStack() { return &m_undo; }

    // ---- media --------------------------------------------------------------
    // Imports a file into the bin. Returns the media id (empty on failure).
    QString importFile(const QString& path, QString* error = nullptr);
    void removeMedia(const QString& id);
    void setMediaMarks(const QString& id, double in, double out);
    void setTranscript(const QString& id, const QVector<TranscriptSegment>& segments, const QString& language, const QString& model);
    // Replaces only the sentences (no undo step: used by TranscriptCommand).
    void setTranscriptSegments(const QString& id, const QVector<TranscriptSegment>& segments);
    // Corrects one sentence by hand (undoable); the sentence is then marked as checked by the user.
    bool editSentence(const QString& mediaId, int index, const QString& text);
    // Replaces a word / phrase in every transcript, ignoring case, diacritics and hamza variants (one undo step).
    // Returns how many places were changed.
    int replaceInTranscripts(const QString& from, const QString& to);

    // ---- selection (clip ids) -------------------------------------------------
    const QSet<QString>& selection() const { return m_selection; }
    void setSelection(const QSet<QString>& ids);
    QString primarySelection() const;

    // ---- editing --------------------------------------------------------------
    // Runs `fn` on a copy of the timeline and pushes an undo step.
    void edit(const QString& text, const std::function<void(Timeline&)>& fn, const QString& mergeKey = QString());
    // For live drags: replace the timeline without an undo step, then call commit().
    void setTimelineLive(const Timeline& t);
    void commit(const QString& text, const Timeline& before);
    // Direct access for the undo command / loading.
    void applyTimeline(const Timeline& t, bool markDirty = true);

    void setSequenceSettings(int w, int h, double fps);

    // ---- files ------------------------------------------------------------------
    bool save(const QString& path, QString* error = nullptr);
    bool load(const QString& path, QString* error = nullptr);
    void clear();

signals:
    void timelineChanged();
    void mediaAdded(const QString& id);
    void mediaRemoved(const QString& id);
    void mediaChanged(const QString& id);
    void selectionChanged();
    void projectReset();
    void dirtyChanged(bool dirty);
    void titleChanged();

private:
    void setDirty(bool d);

    QVector<MediaItem> m_media;
    Timeline m_timeline = Timeline::makeDefault();
    QUndoStack m_undo;
    QSet<QString> m_selection;
    QString m_filePath;
    bool m_dirty = false;
    std::shared_ptr<const Timeline> m_snapshot;
};

}  // namespace hr
