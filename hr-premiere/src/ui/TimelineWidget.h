#pragma once
#include "core/Project.h"
#include "media/MediaAssets.h"

#include <QScrollBar>
#include <QWidget>

namespace hr {

enum class Tool { Selection, TrackForward, Ripple, Razor, Hand, Zoom, Type };

// The timeline: ruler, track headers and clips, with Premiere-style editing tools.
// Everything is painted by hand; edits go through Project so they are undoable.
class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    TimelineWidget(Project* project, MediaAssets* assets, QWidget* parent = nullptr);

    void setTool(Tool t);
    Tool tool() const { return m_tool; }
    void setPlayhead(double t);
    double playhead() const { return m_playhead; }
    void setSnapping(bool on) { m_snap = on; }
    bool snapping() const { return m_snap; }
    void setLinkedSelection(bool on) { m_linked = on; }
    bool linkedSelection() const { return m_linked; }
    void setFollowPlayhead(bool on) { m_follow = on; }

    void zoomIn();
    void zoomOut();
    void zoomToFit();
    void zoomAt(double factor, double anchorTime);
    double pixelsPerSecond() const { return m_pps; }

    QRectF clipRectForId(const QString& clipId);   // widget coordinates (for tests / scripting)
    // Voice-over: shows a red "recording" band on an audio track.
    void setRecording(int audioTrack, double start, double end);
    int targetVideoTrack() const { return m_targetV; }
    int targetAudioTrack() const { return m_targetA; }

    // all clips that are selected (linked partners included when linked selection is on)
    QSet<QString> selectedClips() const { return m_project->selection(); }

signals:
    void seekRequested(double t);
    void mediaDropped(const QStringList& ids, double time, int videoTrack, int audioTrack);
    void filesDropped(const QStringList& paths, double time, int videoTrack, int audioTrack);
    void effectDropped(const QString& clipId, const QString& type);
    void titleRequested(double time, int videoTrack);
    void voiceOverRequested(int audioTrack);
    void statusMessage(const QString& text);
    void contextMenuRequested(const QPoint& globalPos, const QString& clipId);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void leaveEvent(QEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dragMoveEvent(QDragMoveEvent*) override;
    void dragLeaveEvent(QDragLeaveEvent*) override;
    void dropEvent(QDropEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;

private slots:
    void onTimelineChanged();
    void onAssetsUpdated() { update(); }

private:
    struct Row {
        TrackType type;
        int index;
        int y, h;
    };
    enum class Zone { None, Body, LeftEdge, RightEdge };
    struct Hit {
        QString clipId;
        Zone zone = Zone::None;
        int row = -1;
    };
    enum class Drag { None, Scrub, Move, TrimLeft, TrimRight, Marquee, Pan, Marker };

    // geometry
    int headerW() const { return 150; }
    int rulerH() const { return 30; }
    int scrollBarSize() const { return 12; }
    QRect contentRect() const;
    QRect rulerRect() const;
    void layoutRows();
    double xToTime(double x) const { return m_origin + (x - headerW()) / m_pps; }
    double timeToX(double t) const { return headerW() + (t - m_origin) * m_pps; }
    int rowAt(int y) const;
    QRectF clipRect(const Clip& c, const Row& r) const;
    Hit hitTest(const QPoint& p) const;
    int contentHeight() const;
    void updateScrollBars();
    void ensurePlayheadVisible();

    // painting
    void paintRuler(QPainter& p);
    void paintRows(QPainter& p);
    void paintClip(QPainter& p, const Clip& c, const Row& r, bool selected);
    void paintHeaders(QPainter& p);
    void paintOverlays(QPainter& p);

    // editing helpers
    double snapTime(double t, const QSet<QString>& exclude, bool* snapped = nullptr, double extraEnd = -1);
    double quantize(double t) const;
    QSet<QString> withLinked(const QSet<QString>& ids) const;
    void selectClip(const QString& id, bool additive);
    void beginDrag(Drag d, const QPoint& p, const Hit& hit);
    void updateDrag(const QPoint& p, Qt::KeyboardModifiers mods);
    void endDrag();
    void razorAt(const QPoint& p, bool allTracks);
    bool headerClick(const QPoint& p);
    void updateCursor(const QPoint& p);
    double durationForDrop(const QStringList& ids) const;

    Project* m_project;
    MediaAssets* m_assets;
    Tool m_tool = Tool::Selection;
    double m_pps = 80;       // pixels per second
    double m_origin = 0;     // time at the left edge of the clip area
    int m_vScroll = 0;
    double m_playhead = 0;
    bool m_snap = true, m_linked = true, m_follow = true;
    int m_targetV = 0, m_targetA = 0;
    QVector<Row> m_rows;
    QScrollBar* m_hbar;
    QScrollBar* m_vbar;
    bool m_updatingBars = false;

    // interaction state
    Drag m_drag = Drag::None;
    QPoint m_pressPos;
    Hit m_pressHit;
    double m_pressTime = 0;
    Timeline m_before;                // timeline when the drag started
    QSet<QString> m_dragIds;
    TrackType m_dragType = TrackType::Video;
    int m_dragRow = 0;
    bool m_dragMoved = false;
    QRect m_marquee;
    QSet<QString> m_marqueeBase;
    double m_snapLine = -1;           // time of the snap indicator (-1 none)
    QPoint m_mouse;
    bool m_mouseIn = false;
    int m_markerIndex = -1;
    int m_recTrack = -1;
    double m_recStart = 0, m_recEnd = 0;

    // drag & drop preview
    bool m_dropActive = false;
    double m_dropTime = 0, m_dropDur = 0;
    int m_dropRow = 0;
    bool m_dropAudioOnly = false;
};

}  // namespace hr
