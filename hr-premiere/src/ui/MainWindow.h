#pragma once
#include "AiPanel.h"
#include "AudioMeters.h"
#include "EffectControlsPanel.h"
#include "EffectsBrowser.h"
#include "MonitorPanel.h"
#include "ProjectPanel.h"
#include "TimelineWidget.h"
#include "ToolsBar.h"
#include "WorkspaceBar.h"
#include "core/Project.h"
#include "engine/Player.h"
#include "engine/VoiceRecorder.h"
#include "media/MediaAssets.h"

#include <QDockWidget>
#include <QLabel>
#include <QMainWindow>
#include <QTimer>
#include <QToolButton>
#include <QUndoView>

namespace hr {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(bool restoreLayout = true);
    ~MainWindow() override;

    bool openProjectFile(const QString& path);
    QStringList importFiles(const QStringList& paths, bool quiet = false);
    // Imports files and lays them out one after another on V1/A1 (used by the command line).
    void autoPlace(const QStringList& paths);
    void applyWorkspace(const QString& name);
    Project* project() { return &m_project; }
    TimelineWidget* timeline() { return m_timeline; }
    Player* program() { return m_program; }

protected:
    void closeEvent(QCloseEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

private slots:
    void onTimelineChanged();
    void onProgramPosition(double t);

private:
    struct ClipCopy {
        Clip clip;
        TrackType type;
        int track;
    };

    // setup
    void buildUi();
    void buildMenus();
    QAction* addAction(QMenu* menu, const QString& text, const QKeySequence& key, const std::function<void()>& fn);
    QDockWidget* makeDock(const QString& title, const QString& objectName, QWidget* w);

    // project / files
    void newProject();
    void openProject();
    bool saveProject();
    bool saveProjectAs();
    bool maybeSave();
    void importDialog();
    void exportMedia();
    void exportFrame();
    void updateTitle();

    // editing
    Player* activePlayer() const { return m_sourceActive ? m_source : m_program; }
    void togglePlay();
    void addMediaToTimeline(const QStringList& ids, double time, int v, int a, bool insert, double srcIn = -1, double dur = -1);
    void insertFromSource(bool insert);
    void loadSource(const QString& mediaId);
    void deleteSelection(bool ripple);
    void splitAtPlayhead(bool allTracks);
    void copySelection(bool cut);
    void paste();
    void selectAll();
    void toggleLink();
    void addMarker();
    void setMark(bool in);
    void clearMarks();
    void addTitle(double time, int track);
    void applyEffect(const QString& clipId, const QString& type);
    void applyEffectToSelection(const QString& type);
    void setFade(bool fadeIn);
    void toggleClipEnabled();
    void changeSpeed();
    void preferences();
    void restartApplication();
    void removeEffectsFromSelection();
    void sequenceSettings();
    void addTrack(TrackType t);
    void removeLastTrack(TrackType t);
    void setTool(Tool t);
    void showClipMenu(const QPoint& globalPos, const QString& clipId);
    void setStatus(const QString& text);
    void updateDockTitles();

    // voice-over
    void toggleVoiceOver(int audioTrack);
    void beginVoiceOver();
    void updateRecIndicator();
    void finishVoiceOver(bool keep);
    QString voiceOverPath() const;

    Project m_project;
    MediaAssets m_assets;
    Player* m_program;
    Player* m_source;
    bool m_sourceActive = false;
    QString m_sourceMedia;

    WorkspaceBar* m_workspaceBar = nullptr;
    MonitorPanel* m_programMon = nullptr;
    MonitorPanel* m_sourceMon = nullptr;
    TimelineWidget* m_timeline = nullptr;
    ToolsBar* m_tools = nullptr;
    AudioMeters* m_meters = nullptr;
    ProjectPanel* m_projectPanel = nullptr;
    EffectControlsPanel* m_effectControls = nullptr;
    EffectsBrowser* m_effectsBrowser = nullptr;
    QUndoView* m_history = nullptr;
    QLabel* m_tlTimecode = nullptr;
    QLabel* m_tlName = nullptr;
    QToolButton* m_snapBtn = nullptr;
    QToolButton* m_linkBtn = nullptr;
    QLabel* m_status = nullptr;

    QDockWidget *m_dockSource = nullptr, *m_dockEffectControls = nullptr, *m_dockProgram = nullptr,
                *m_dockProject = nullptr, *m_dockEffects = nullptr, *m_dockHistory = nullptr, *m_dockAi = nullptr;
    AiPanel* m_ai = nullptr;

    VoiceRecorder m_recorder;
    QTimer* m_voTimer = nullptr;       // refreshes the REC indicator and the recording band
    QTimer* m_voCountdown = nullptr;
    QLabel* m_recLabel = nullptr;
    int m_voTrack = 0;
    int m_voCount = 0;
    double m_voStart = 0;
    QString m_voPath;
    bool m_voRecording = false;

    QVector<ClipCopy> m_clipboard;
    bool m_restoreLayout;
    bool m_restarting = false;
    QString m_workspace = "Editing";
};

}  // namespace hr
