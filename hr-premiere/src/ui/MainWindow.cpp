#include "MainWindow.h"

#include "Dialogs.h"
#include "Icons.h"
#include "Theme.h"
#include "core/EditOps.h"
#include "core/Util.h"
#include "engine/Renderer.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QScreen>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <cmath>

namespace hr {

static const char* kMediaFilter =
    "Media files (*.mp4 *.mov *.mkv *.avi *.webm *.m4v *.mpg *.mpeg *.mts *.m2ts *.wmv *.flv *.3gp "
    "*.mp3 *.wav *.aac *.m4a *.flac *.ogg *.opus *.wma *.png *.jpg *.jpeg *.bmp *.webp *.tif *.tiff);;All files (*)";

MainWindow::MainWindow(bool restoreLayout) : m_restoreLayout(restoreLayout) {
    setWindowTitle("HR Premiere");
    setWindowIcon(QIcon(icons::logo(64)));
    setAcceptDrops(true);
    setDockNestingEnabled(true);
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
    setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    resize(1500, 900);

    m_program = new Player(this);
    m_source = new Player(this);
    m_program->setTimeline(m_project.snapshot());

    buildUi();
    buildMenus();

    connect(&m_project, &Project::timelineChanged, this, &MainWindow::onTimelineChanged);
    connect(&m_project, &Project::dirtyChanged, this, [this] { updateTitle(); });
    connect(&m_project, &Project::titleChanged, this, [this] { updateTitle(); });
    connect(&m_project, &Project::projectReset, this, [this] {
        m_sourceMedia.clear();
        m_source->pause();
        m_source->setTimeline(std::make_shared<const Timeline>(Timeline::makeDefault(1, 1)));
        m_sourceMon->setPlaceholder("Double-click a clip in the Project panel\nto load it here");
        m_sourceMon->syncDuration();
        m_dockSource->setWindowTitle("Source: (no clips)");
        m_program->seek(0);
        m_timeline->zoomToFit();
    });
    connect(m_program, &Player::positionChanged, this, &MainWindow::onProgramPosition);
    connect(m_program, &Player::levels, m_meters, &AudioMeters::setLevels);
    connect(m_source, &Player::levels, m_meters, &AudioMeters::setLevels);
    connect(m_program, &Player::playingChanged, this, [this](bool playing) {
        if (playing) m_source->pause();
        m_timeline->setFollowPlayhead(true);
    });
    connect(m_source, &Player::playingChanged, this, [this](bool playing) { if (playing) m_program->pause(); });

    onTimelineChanged();
    m_sourceMon->setPlaceholder("Double-click a clip in the Project panel\nto load it here");
    applyWorkspace("Editing");
    if (m_restoreLayout) {
        QSettings s;
        const QByteArray st = s.value("layout/state").toByteArray();
        if (!st.isEmpty()) restoreState(st, 3);
        const QByteArray geo = s.value("layout/geometry").toByteArray();
        if (!geo.isEmpty()) restoreGeometry(geo);
    }
    updateDockTitles();
    updateTitle();
    setStatus("Ready");
}

MainWindow::~MainWindow() = default;

QDockWidget* MainWindow::makeDock(const QString& title, const QString& objectName, QWidget* w) {
    auto* d = new QDockWidget(title, this);
    d->setObjectName(objectName);
    d->setWidget(w);
    d->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    return d;
}

// ============================================================================ UI
void MainWindow::buildUi() {
    // --- workspace bar -----------------------------------------------------
    m_workspaceBar = new WorkspaceBar(this);
    connect(m_workspaceBar, &WorkspaceBar::workspaceSelected, this, &MainWindow::applyWorkspace);

    // --- monitors -----------------------------------------------------------
    m_programMon = new MonitorPanel(m_program, MonitorPanel::Program, this);
    m_sourceMon = new MonitorPanel(m_source, MonitorPanel::Source, this);
    m_sourceMon->setSequenceSize(QSize(1920, 1080));
    connect(m_programMon, &MonitorPanel::interacted, this, [this] { m_sourceActive = false; });
    connect(m_sourceMon, &MonitorPanel::interacted, this, [this] { m_sourceActive = true; });
    connect(m_programMon, &MonitorPanel::markInRequested, this, [this](double) { setMark(true); });
    connect(m_programMon, &MonitorPanel::markOutRequested, this, [this](double) { setMark(false); });
    connect(m_programMon, &MonitorPanel::exportFrameRequested, this, &MainWindow::exportFrame);
    connect(m_sourceMon, &MonitorPanel::exportFrameRequested, this, &MainWindow::exportFrame);
    connect(m_sourceMon, &MonitorPanel::insertRequested, this, [this] { insertFromSource(true); });
    connect(m_sourceMon, &MonitorPanel::overwriteRequested, this, [this] { insertFromSource(false); });
    connect(m_sourceMon, &MonitorPanel::markInRequested, this, [this](double t) {
        if (const MediaItem* m = m_project.mediaById(m_sourceMedia)) {
            double out = m->markOut;
            if (out >= 0 && out <= t) out = -1;
            m_project.setMediaMarks(m_sourceMedia, t, out);
            m_sourceMon->setMarks(t, out);
        }
    });
    connect(m_sourceMon, &MonitorPanel::markOutRequested, this, [this](double t) {
        if (const MediaItem* m = m_project.mediaById(m_sourceMedia)) {
            double in = m->markIn;
            if (in >= 0 && in >= t) in = -1;
            m_project.setMediaMarks(m_sourceMedia, in, t);
            m_sourceMon->setMarks(in, t);
        }
    });

    // --- panels -------------------------------------------------------------
    m_projectPanel = new ProjectPanel(&m_project, &m_assets, this);
    m_effectControls = new EffectControlsPanel(&m_project, this);
    m_effectsBrowser = new EffectsBrowser(this);
    m_history = new QUndoView(m_project.undoStack(), this);
    m_history->setEmptyLabel("Original");

    connect(m_projectPanel, &ProjectPanel::importRequested, this, &MainWindow::importDialog);
    connect(m_projectPanel, &ProjectPanel::filesDropped, this, [this](const QStringList& p) { importFiles(p); });
    connect(m_projectPanel, &ProjectPanel::openInSource, this, &MainWindow::loadSource);
    connect(m_projectPanel, &ProjectPanel::insertRequested, this, [this](const QStringList& ids) {
        addMediaToTimeline(ids, m_program->position(), m_timeline->targetVideoTrack(), m_timeline->targetAudioTrack(), true);
    });
    connect(m_projectPanel, &ProjectPanel::overwriteRequested, this, [this](const QStringList& ids) {
        addMediaToTimeline(ids, m_program->position(), m_timeline->targetVideoTrack(), m_timeline->targetAudioTrack(), false);
    });
    connect(m_projectPanel, &ProjectPanel::removeRequested, this, [this](const QStringList& ids) {
        if (ids.isEmpty()) return;
        // clips that use the media stay on the timeline (they keep their own path)
        for (const QString& id : ids) {
            m_assets.forget(id);
            m_project.removeMedia(id);
        }
    });
    connect(m_effectsBrowser, &EffectsBrowser::applyEffect, this, &MainWindow::applyEffectToSelection);

    // --- docks ----------------------------------------------------------------
    m_dockSource = makeDock("Source: (no clips)", "dock_source", m_sourceMon);
    m_dockEffectControls = makeDock("Effect Controls", "dock_effect_controls", m_effectControls);
    m_dockProgram = makeDock("Program: Sequence 01", "dock_program", m_programMon);
    m_dockProject = makeDock("Project", "dock_project", m_projectPanel);
    m_dockEffects = makeDock("Effects", "dock_effects", m_effectsBrowser);
    m_dockHistory = makeDock("History", "dock_history", m_history);

    addDockWidget(Qt::LeftDockWidgetArea, m_dockSource);
    addDockWidget(Qt::LeftDockWidgetArea, m_dockProject);
    splitDockWidget(m_dockSource, m_dockProject, Qt::Vertical);
    tabifyDockWidget(m_dockSource, m_dockEffectControls);
    tabifyDockWidget(m_dockProject, m_dockEffects);
    tabifyDockWidget(m_dockProject, m_dockHistory);
    addDockWidget(Qt::TopDockWidgetArea, m_dockProgram);
    m_dockSource->raise();
    m_dockProject->raise();
    for (QDockWidget* d : {m_dockSource, m_dockEffectControls, m_dockProgram, m_dockProject, m_dockEffects, m_dockHistory}) {
        connect(d, &QDockWidget::dockLocationChanged, this, [this] { QTimer::singleShot(0, this, &MainWindow::updateDockTitles); });
        connect(d, &QDockWidget::topLevelChanged, this, [this] { QTimer::singleShot(0, this, &MainWindow::updateDockTitles); });
    }
    updateDockTitles();

    // --- timeline panel (central) ----------------------------------------------
    m_timeline = new TimelineWidget(&m_project, &m_assets, this);
    m_tools = new ToolsBar(this);
    m_meters = new AudioMeters(this);

    auto* header = new QWidget(this);
    header->setFixedHeight(36);
    header->setStyleSheet(QString("background:%1;").arg(theme::bg2.name()));
    auto* hl = new QHBoxLayout(header);
    hl->setContentsMargins(10, 0, 10, 0);
    hl->setSpacing(6);
    m_tlName = new QLabel("Sequence 01");
    m_tlName->setStyleSheet("font-weight:600;");
    m_tlTimecode = new QLabel("00:00:00:00");
    m_tlTimecode->setStyleSheet(QString("color:%1; font-size:18px; font-weight:600; font-family:'DejaVu Sans Mono',Consolas,monospace;")
                                    .arg(theme::accentHi.name()));
    auto tbtn = [&](icons::Name n, const QString& tip, bool checkable = false) {
        auto* b = new QToolButton(header);
        b->setIcon(icons::make(n, theme::text, QColor("#ffffff")));
        b->setIconSize(QSize(18, 18));
        b->setToolTip(tip);
        b->setCheckable(checkable);
        return b;
    };
    m_snapBtn = tbtn(icons::Magnet, "Snap (S)", true);
    m_snapBtn->setChecked(true);
    m_linkBtn = tbtn(icons::Link, "Linked Selection (Ctrl+L toggles link of selected clips)", true);
    m_linkBtn->setChecked(true);
    auto* markerBtn = tbtn(icons::Marker, "Add Marker (M)");
    auto* zoomOut = tbtn(icons::Zoom, "Zoom out (-)");
    auto* fitBtn = tbtn(icons::Film, "Zoom to fit (\\)");
    hl->addWidget(m_tlName);
    hl->addSpacing(16);
    hl->addWidget(m_tlTimecode);
    hl->addStretch();
    hl->addWidget(m_snapBtn);
    hl->addWidget(m_linkBtn);
    hl->addWidget(markerBtn);
    hl->addSpacing(10);
    hl->addWidget(zoomOut);
    hl->addWidget(fitBtn);
    zoomOut->setIcon(icons::make(icons::Zoom));

    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    row->addWidget(m_tools);
    row->addWidget(m_timeline, 1);
    row->addWidget(m_meters);

    auto* central = new QWidget(this);
    auto* cl = new QVBoxLayout(central);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(0);
    cl->addWidget(header);
    cl->addLayout(row, 1);
    setCentralWidget(central);

    // wiring of the timeline
    connect(m_snapBtn, &QToolButton::toggled, m_timeline, &TimelineWidget::setSnapping);
    connect(m_linkBtn, &QToolButton::toggled, m_timeline, &TimelineWidget::setLinkedSelection);
    connect(markerBtn, &QToolButton::clicked, this, &MainWindow::addMarker);
    connect(zoomOut, &QToolButton::clicked, m_timeline, &TimelineWidget::zoomOut);
    connect(fitBtn, &QToolButton::clicked, m_timeline, &TimelineWidget::zoomToFit);
    connect(m_tools, &ToolsBar::toolSelected, this, [this](Tool t) { setTool(t); });
    connect(m_timeline, &TimelineWidget::seekRequested, this, [this](double t) {
        m_sourceActive = false;
        if (m_program->isPlaying()) m_program->pause();
        m_program->seek(t);
    });
    connect(m_timeline, &TimelineWidget::mediaDropped, this,
            [this](const QStringList& ids, double t, int v, int a) { addMediaToTimeline(ids, t, v, a, false); });
    connect(m_timeline, &TimelineWidget::filesDropped, this, [this](const QStringList& paths, double t, int v, int a) {
        const QStringList ids = importFiles(paths);
        if (!ids.isEmpty()) addMediaToTimeline(ids, t, v, a, false);
    });
    connect(m_timeline, &TimelineWidget::effectDropped, this, &MainWindow::applyEffect);
    connect(m_timeline, &TimelineWidget::titleRequested, this, &MainWindow::addTitle);
    connect(m_timeline, &TimelineWidget::statusMessage, this, [this](const QString& s) { setStatus(s.isEmpty() ? "Ready" : s); });
    connect(m_timeline, &TimelineWidget::contextMenuRequested, this, &MainWindow::showClipMenu);

    m_status = new QLabel;
    statusBar()->addWidget(m_status, 1);
    statusBar()->setSizeGripEnabled(false);
}

QAction* MainWindow::addAction(QMenu* menu, const QString& text, const QKeySequence& key, const std::function<void()>& fn) {
    QAction* a = menu->addAction(text);
    if (!key.isEmpty()) a->setShortcut(key);
    a->setShortcutContext(Qt::WindowShortcut);
    connect(a, &QAction::triggered, this, [fn] { fn(); });
    return a;
}

void MainWindow::buildMenus() {
    // the workspace bar sits right below the menu bar
    auto* wrap = new QWidget(this);
    QMenuBar* mb = new QMenuBar(wrap);
    auto* wl = new QVBoxLayout(wrap);
    wl->setContentsMargins(0, 0, 0, 0);
    wl->setSpacing(0);
    wl->addWidget(mb);
    wl->addWidget(m_workspaceBar);
    setMenuWidget(wrap);

    // ---- File
    QMenu* file = mb->addMenu("&File");
    addAction(file, "New Project", QKeySequence("Ctrl+Alt+N"), [this] { newProject(); });
    addAction(file, "Open Project…", QKeySequence::Open, [this] { openProject(); });
    file->addSeparator();
    addAction(file, "Save", QKeySequence::Save, [this] { saveProject(); });
    addAction(file, "Save As…", QKeySequence("Ctrl+Shift+S"), [this] { saveProjectAs(); });
    file->addSeparator();
    addAction(file, "Import…", QKeySequence("Ctrl+I"), [this] { importDialog(); });
    addAction(file, "Export Media…", QKeySequence("Ctrl+M"), [this] { exportMedia(); });
    addAction(file, "Export Frame…", QKeySequence("Ctrl+Shift+E"), [this] { exportFrame(); });
    file->addSeparator();
    addAction(file, "Exit", QKeySequence::Quit, [this] { close(); });

    // ---- Edit
    QMenu* edit = mb->addMenu("&Edit");
    QAction* undo = m_project.undoStack()->createUndoAction(this, "Undo");
    undo->setShortcuts({QKeySequence("Ctrl+Z")});
    QAction* redo = m_project.undoStack()->createRedoAction(this, "Redo");
    redo->setShortcuts({QKeySequence("Ctrl+Shift+Z"), QKeySequence("Ctrl+Y")});
    edit->addAction(undo);
    edit->addAction(redo);
    edit->addSeparator();
    addAction(edit, "Cut", QKeySequence("Ctrl+X"), [this] { copySelection(true); });
    addAction(edit, "Copy", QKeySequence("Ctrl+C"), [this] { copySelection(false); });
    addAction(edit, "Paste", QKeySequence("Ctrl+V"), [this] { paste(); });
    edit->addSeparator();
    QAction* del = addAction(edit, "Delete", QKeySequence(Qt::Key_Delete), [this] { deleteSelection(false); });
    del->setShortcuts({QKeySequence(Qt::Key_Delete), QKeySequence(Qt::Key_Backspace)});
    QAction* rdel = addAction(edit, "Ripple Delete", QKeySequence("Shift+Delete"), [this] { deleteSelection(true); });
    rdel->setShortcuts({QKeySequence("Shift+Delete"), QKeySequence("Shift+Backspace")});
    edit->addSeparator();
    addAction(edit, "Select All", QKeySequence("Ctrl+A"), [this] { selectAll(); });
    addAction(edit, "Deselect All", QKeySequence("Ctrl+Shift+A"), [this] { m_project.setSelection({}); });

    // ---- Clip
    QMenu* clip = mb->addMenu("&Clip");
    addAction(clip, "Add Edit at Playhead", QKeySequence("Ctrl+K"), [this] { splitAtPlayhead(false); });
    addAction(clip, "Add Edit to All Tracks", QKeySequence("Ctrl+Shift+K"), [this] { splitAtPlayhead(true); });
    clip->addSeparator();
    addAction(clip, "Link / Unlink", QKeySequence("Ctrl+L"), [this] { toggleLink(); });
    addAction(clip, "Enable / Disable Clip", QKeySequence("Shift+E"), [this] { toggleClipEnabled(); });
    clip->addSeparator();
    addAction(clip, "Fade In (1 s)", QKeySequence(), [this] { setFade(true); });
    addAction(clip, "Fade Out (1 s)", QKeySequence(), [this] { setFade(false); });
    addAction(clip, "Remove All Effects", QKeySequence(), [this] { removeEffectsFromSelection(); });

    // ---- Graphics
    QMenu* gfx = mb->addMenu("&Graphics");
    addAction(gfx, "New Title at Playhead", QKeySequence("Ctrl+T"), [this] { addTitle(m_program->position(), m_timeline->targetVideoTrack()); });

    // ---- Sequence
    QMenu* seq = mb->addMenu("&Sequence");
    addAction(seq, "Sequence Settings…", QKeySequence(), [this] { sequenceSettings(); });
    seq->addSeparator();
    addAction(seq, "Insert from Source", QKeySequence(Qt::Key_Comma), [this] { insertFromSource(true); });
    addAction(seq, "Overwrite from Source", QKeySequence(Qt::Key_Period), [this] { insertFromSource(false); });
    seq->addSeparator();
    addAction(seq, "Mark In", QKeySequence(Qt::Key_I), [this] { setMark(true); });
    addAction(seq, "Mark Out", QKeySequence(Qt::Key_O), [this] { setMark(false); });
    addAction(seq, "Clear In and Out", QKeySequence("Ctrl+Shift+X"), [this] { clearMarks(); });
    addAction(seq, "Add Marker", QKeySequence(Qt::Key_M), [this] { addMarker(); });
    seq->addSeparator();
    addAction(seq, "Go to Previous Edit", QKeySequence(Qt::Key_Up), [this] {
        activePlayer()->seek(m_sourceActive ? 0.0 : ops::prevEdit(m_program->timeline(), m_program->position()));
    });
    addAction(seq, "Go to Next Edit", QKeySequence(Qt::Key_Down), [this] {
        activePlayer()->seek(m_sourceActive ? activePlayer()->duration() : ops::nextEdit(m_program->timeline(), m_program->position()));
    });
    seq->addSeparator();
    addAction(seq, "Add Video Track", QKeySequence(), [this] { addTrack(TrackType::Video); });
    addAction(seq, "Add Audio Track", QKeySequence(), [this] { addTrack(TrackType::Audio); });
    addAction(seq, "Delete Last Video Track", QKeySequence(), [this] { removeLastTrack(TrackType::Video); });
    addAction(seq, "Delete Last Audio Track", QKeySequence(), [this] { removeLastTrack(TrackType::Audio); });
    seq->addSeparator();
    addAction(seq, "Zoom In", QKeySequence(Qt::Key_Equal), [this] { m_timeline->zoomIn(); })->setShortcuts({QKeySequence(Qt::Key_Equal), QKeySequence(Qt::Key_Plus)});
    addAction(seq, "Zoom Out", QKeySequence(Qt::Key_Minus), [this] { m_timeline->zoomOut(); });
    addAction(seq, "Zoom to Fit", QKeySequence(Qt::Key_Backslash), [this] { m_timeline->zoomToFit(); });
    addAction(seq, "Toggle Snapping", QKeySequence(Qt::Key_S), [this] { m_snapBtn->toggle(); });

    // ---- Tools (shortcuts only; shown in the Sequence menu's sibling)
    QMenu* tools = mb->addMenu("&Tools");
    addAction(tools, "Selection Tool", QKeySequence(Qt::Key_V), [this] { setTool(Tool::Selection); });
    addAction(tools, "Track Select Forward Tool", QKeySequence(Qt::Key_A), [this] { setTool(Tool::TrackForward); });
    addAction(tools, "Ripple Edit Tool", QKeySequence(Qt::Key_B), [this] { setTool(Tool::Ripple); });
    addAction(tools, "Razor Tool", QKeySequence(Qt::Key_C), [this] { setTool(Tool::Razor); });
    addAction(tools, "Hand Tool", QKeySequence(Qt::Key_H), [this] { setTool(Tool::Hand); });
    addAction(tools, "Zoom Tool", QKeySequence(Qt::Key_Z), [this] { setTool(Tool::Zoom); });
    addAction(tools, "Type Tool", QKeySequence(Qt::Key_T), [this] { setTool(Tool::Type); });

    // ---- Playback shortcuts (in a hidden group so they show up in the Tools menu)
    tools->addSeparator();
    addAction(tools, "Play / Stop", QKeySequence(Qt::Key_Space), [this] { togglePlay(); });
    addAction(tools, "Step Back", QKeySequence(Qt::Key_Left), [this] { activePlayer()->stepFrames(-1); });
    addAction(tools, "Step Forward", QKeySequence(Qt::Key_Right), [this] { activePlayer()->stepFrames(1); });
    addAction(tools, "Step Back 5 Frames", QKeySequence("Shift+Left"), [this] { activePlayer()->stepFrames(-5); });
    addAction(tools, "Step Forward 5 Frames", QKeySequence("Shift+Right"), [this] { activePlayer()->stepFrames(5); });
    addAction(tools, "Go to Start", QKeySequence(Qt::Key_Home), [this] { activePlayer()->seek(0); });
    addAction(tools, "Go to End", QKeySequence(Qt::Key_End), [this] {
        activePlayer()->seek(std::max(0.0, activePlayer()->duration() - 1.0 / activePlayer()->fps()));
    });

    // ---- Window
    QMenu* win = mb->addMenu("&Window");
    QMenu* ws = win->addMenu("Workspaces");
    for (const QString& n : m_workspaceBar->workspaces()) addAction(ws, n, QKeySequence(), [this, n] { applyWorkspace(n); });
    ws->addSeparator();
    addAction(ws, "Reset to Saved Layout", QKeySequence(), [this] { applyWorkspace(m_workspace); });
    win->addSeparator();
    for (QDockWidget* d : {m_dockSource, m_dockEffectControls, m_dockProgram, m_dockProject, m_dockEffects, m_dockHistory})
        win->addAction(d->toggleViewAction());

    // ---- Help
    QMenu* help = mb->addMenu("&Help");
    addAction(help, "Keyboard Shortcuts", QKeySequence("Ctrl+/"), [this] { ShortcutsDialog(this).exec(); });
    addAction(help, "About HR Premiere", QKeySequence(), [this] { AboutDialog(this).exec(); });
}

// ============================================================================ workspaces
void MainWindow::applyWorkspace(const QString& name) {
    m_workspace = name;
    m_workspaceBar->setCurrent(name);
    for (QDockWidget* d : {m_dockSource, m_dockEffectControls, m_dockProgram, m_dockProject, m_dockEffects, m_dockHistory}) {
        d->setFloating(false);
        d->show();
    }
    const int W = std::max(1000, width()), H = std::max(700, height());
    struct L { double leftW, programH, sourceH; QDockWidget* top; QDockWidget* bottom; };
    L l{0.36, 0.52, 0.5, m_dockSource, m_dockProject};
    if (name == "Assembly") l = {0.52, 0.42, 0.34, m_dockSource, m_dockProject};
    else if (name == "Color") l = {0.34, 0.55, 0.68, m_dockEffectControls, m_dockProject};
    else if (name == "Effects") l = {0.36, 0.52, 0.55, m_dockEffectControls, m_dockEffects};
    else if (name == "Audio") l = {0.30, 0.36, 0.45, m_dockSource, m_dockProject};
    l.top->raise();
    l.bottom->raise();
    const int body = H - 120;
    resizeDocks({m_dockSource, m_dockProject}, {int(body * l.sourceH), int(body * (1 - l.sourceH))}, Qt::Vertical);
    resizeDocks({m_dockSource}, {int(W * l.leftW)}, Qt::Horizontal);
    resizeDocks({m_dockProgram}, {int(body * l.programH)}, Qt::Vertical);
}

void MainWindow::updateDockTitles() {
    // Tabbed panels already show their name on the tab, so their own title bar is hidden;
    // panels that stand alone (or float) keep the normal title bar.
    for (QDockWidget* d : {m_dockSource, m_dockEffectControls, m_dockProgram, m_dockProject, m_dockEffects, m_dockHistory}) {
        const bool tabbed = !d->isFloating() && !tabifiedDockWidgets(d).isEmpty();
        if (tabbed) {
            if (!d->titleBarWidget()) {
                auto* empty = new QWidget(d);
                empty->setFixedHeight(0);
                d->setTitleBarWidget(empty);
            }
        } else if (QWidget* old = d->titleBarWidget()) {
            d->setTitleBarWidget(nullptr);
            old->deleteLater();
        }
    }
}

void MainWindow::setTool(Tool t) {
    m_timeline->setTool(t);
    m_tools->setTool(t);
    static const char* names[] = {"Selection", "Track Select Forward", "Ripple Edit", "Razor", "Hand", "Zoom", "Type"};
    setStatus(QString("Tool: %1").arg(names[int(t)]));
}

void MainWindow::setStatus(const QString& text) {
    if (m_status) m_status->setText(text);
}

// ============================================================================ project / files
void MainWindow::updateTitle() {
    const QString n = m_project.name();
    setWindowTitle(QString("%1%2 – HR Premiere").arg(n, m_project.isDirty() ? " *" : ""));
    m_workspaceBar->setProjectName(n, m_project.isDirty());
}

bool MainWindow::maybeSave() {
    if (!m_project.isDirty()) return true;
    const auto r = QMessageBox::question(this, "HR Premiere", QString("Save changes to \"%1\" before closing?").arg(m_project.name()),
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Cancel) return false;
    if (r == QMessageBox::Save) return saveProject();
    return true;
}

void MainWindow::newProject() {
    if (!maybeSave()) return;
    m_project.clear();
}

void MainWindow::openProject() {
    if (!maybeSave()) return;
    const QString path = QFileDialog::getOpenFileName(this, "Open Project", QSettings().value("project/dir").toString(),
                                                      "HR Premiere project (*.hrproj);;All files (*)");
    if (!path.isEmpty()) openProjectFile(path);
}

bool MainWindow::openProjectFile(const QString& path) {
    QString err;
    if (!m_project.load(path, &err)) {
        QMessageBox::warning(this, "Open Project", QString("Could not open the project:\n%1").arg(err));
        return false;
    }
    QSettings().setValue("project/dir", QFileInfo(path).absolutePath());
    for (const MediaItem& m : m_project.media()) m_assets.request(m);
    int offline = 0;
    for (const MediaItem& m : m_project.media()) offline += m.offline ? 1 : 0;
    if (offline) setStatus(QString("%1 media file(s) could not be found and are shown as offline").arg(offline));
    return true;
}

bool MainWindow::saveProject() {
    if (m_project.filePath().isEmpty()) return saveProjectAs();
    QString err;
    if (!m_project.save(m_project.filePath(), &err)) {
        QMessageBox::warning(this, "Save Project", QString("Could not save the project:\n%1").arg(err));
        return false;
    }
    setStatus("Project saved");
    return true;
}

bool MainWindow::saveProjectAs() {
    QString path = QFileDialog::getSaveFileName(this, "Save Project", QDir(QSettings().value("project/dir", QDir::homePath()).toString()).filePath(m_project.name() + ".hrproj"),
                                                "HR Premiere project (*.hrproj)");
    if (path.isEmpty()) return false;
    if (!path.endsWith(".hrproj", Qt::CaseInsensitive)) path += ".hrproj";
    QString err;
    if (!m_project.save(path, &err)) {
        QMessageBox::warning(this, "Save Project", QString("Could not save the project:\n%1").arg(err));
        return false;
    }
    QSettings().setValue("project/dir", QFileInfo(path).absolutePath());
    setStatus("Project saved");
    return true;
}

void MainWindow::importDialog() {
    const QStringList files = QFileDialog::getOpenFileNames(this, "Import Media", QSettings().value("import/dir").toString(), kMediaFilter);
    if (files.isEmpty()) return;
    QSettings().setValue("import/dir", QFileInfo(files.first()).absolutePath());
    importFiles(files);
}

QStringList MainWindow::importFiles(const QStringList& paths, bool quiet) {
    QStringList ids, errors;
    for (const QString& p : paths) {
        QString err;
        const QString id = m_project.importFile(p, &err);
        if (id.isEmpty()) errors << QString("%1 – %2").arg(QFileInfo(p).fileName(), err);
        else ids << id;
    }
    for (const QString& id : ids)
        if (const MediaItem* m = m_project.mediaById(id)) m_assets.request(*m);
    if (!errors.isEmpty() && !quiet)
        QMessageBox::warning(this, "Import", QString("Some files could not be imported:\n\n%1").arg(errors.join('\n')));
    if (!ids.isEmpty()) setStatus(QString("Imported %1 file%2").arg(ids.size()).arg(ids.size() == 1 ? "" : "s"));
    return ids;
}

void MainWindow::autoPlace(const QStringList& paths) {
    const QStringList ids = importFiles(paths, true);
    double t = 0;
    for (const QString& id : ids) {
        const MediaItem* m = m_project.mediaById(id);
        if (!m) continue;
        const double d = m->duration;
        addMediaToTimeline({id}, t, 0, 0, false);
        t += d;
    }
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (!maybeSave()) {
        e->ignore();
        return;
    }
    m_program->pause();
    m_source->pause();
    if (m_restoreLayout) {
        QSettings s;
        s.setValue("layout/state", saveState(3));
        s.setValue("layout/geometry", saveGeometry());
    }
    e->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}
void MainWindow::dropEvent(QDropEvent* e) {
    QStringList paths;
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile()) paths << u.toLocalFile();
    if (paths.size() == 1 && paths.first().endsWith(".hrproj", Qt::CaseInsensitive)) {
        if (maybeSave()) openProjectFile(paths.first());
    } else if (!paths.isEmpty()) {
        importFiles(paths);
    }
}

void MainWindow::exportMedia() {
    if (m_program->isPlaying()) m_program->pause();
    ExportDialog dlg(m_project.timeline(), m_project.name() == "Untitled" ? m_project.timeline().name : m_project.name(), this);
    dlg.exec();
}

void MainWindow::exportFrame() {
    Player* p = activePlayer();
    const Timeline& tl = p->timeline();
    const QString path = QFileDialog::getSaveFileName(this, "Export Frame", QDir::home().filePath("frame.png"), "PNG image (*.png);;JPEG image (*.jpg)");
    if (path.isEmpty()) return;
    DecoderPool pool;
    Renderer r(&pool);
    const QImage img = r.render(tl, snapToFrame(p->position(), tl.fps), QSize(tl.width, tl.height));
    if (img.save(path)) setStatus(QString("Frame saved to %1").arg(path));
    else QMessageBox::warning(this, "Export Frame", "Could not save the image.");
}

// ============================================================================ timeline / players
void MainWindow::onTimelineChanged() {
    const Timeline& tl = m_project.timeline();
    m_program->setTimeline(m_project.snapshot());
    m_programMon->setSequenceSize(QSize(tl.width, tl.height));
    m_programMon->setMarks(tl.markIn, tl.markOut);
    m_programMon->syncDuration();
    m_program->refresh();
    m_tlName->setText(tl.name);
    m_dockProgram->setWindowTitle(QString("Program: %1").arg(tl.name));
}

void MainWindow::onProgramPosition(double t) {
    m_timeline->setPlayhead(t);
    m_tlTimecode->setText(formatTimecode(t, m_program->fps()));
}

void MainWindow::togglePlay() { activePlayer()->toggle(); }

void MainWindow::loadSource(const QString& id) {
    const MediaItem* m = m_project.mediaById(id);
    if (!m || m->offline) return;
    m_sourceMedia = id;
    m_source->pause();
    Timeline t = Timeline::makeDefault(1, 1);
    t.width = m->width > 0 ? m->width : 1920;
    t.height = m->height > 0 ? m->height : 1080;
    t.fps = m->fps > 0 ? std::min(120.0, m->fps) : 30.0;
    ops::placeClips(t, ops::makeClips(*m, 0, m->duration, 0), 0, 0, false);
    m_source->setTimeline(std::make_shared<const Timeline>(t));
    m_sourceMon->setSequenceSize(QSize(t.width, t.height));
    m_sourceMon->setPlaceholder(QString());
    m_sourceMon->setMarks(m->markIn, m->markOut);
    m_sourceMon->syncDuration();
    m_source->seek(0);
    m_dockSource->setWindowTitle(QString("Source: %1").arg(m->name));
    m_dockSource->show();
    m_dockSource->raise();
    m_sourceActive = true;
}

void MainWindow::insertFromSource(bool insert) {
    const MediaItem* m = m_project.mediaById(m_sourceMedia);
    if (!m) {
        setStatus("Load a clip in the Source monitor first (double-click it in the Project panel)");
        return;
    }
    const double in = std::max(0.0, m->markIn);
    const double out = m->markOut >= 0 ? std::min(m->markOut, m->duration) : m->duration;
    if (out - in < 0.05) return;
    addMediaToTimeline({m->id}, m_program->position(), m_timeline->targetVideoTrack(), m_timeline->targetAudioTrack(), insert, in, out - in);
}

void MainWindow::addMediaToTimeline(const QStringList& ids, double time, int v, int a, bool insert, double srcIn, double dur) {
    if (ids.isEmpty()) return;
    const bool wasEmpty = m_project.timeline().isEmpty();
    QSet<QString> added;
    m_project.edit(insert ? "Insert" : "Overwrite", [&](Timeline& tl) {
        double t = snapToFrame(std::max(0.0, time), tl.fps);
        bool first = true;
        for (const QString& id : ids) {
            const MediaItem* m = m_project.mediaById(id);
            if (!m || m->offline) continue;
            if (first && wasEmpty && m->hasVideo && m->kind == MediaKind::Video && m->width > 0 && m->height > 0) {
                // like Premiere: the first clip sets the sequence settings
                tl.width = m->width;
                tl.height = m->height;
                if (m->fps >= 1 && m->fps <= 120) tl.fps = m->fps;
            }
            first = false;
            const double in = srcIn >= 0 ? srcIn : 0.0;
            double d = dur > 0 ? dur : m->duration - in;
            d = std::max(1.0 / tl.fps, snapToFrame(d, tl.fps));
            const QVector<Clip> clips = ops::makeClips(*m, in, d, t);
            for (const Clip& c : clips) added.insert(c.id);
            ops::placeClips(tl, clips, v, a, insert);
            t += d;
        }
    });
    QSet<QString> sel;
    for (const QString& id : added)
        if (m_project.timeline().find(id).valid()) sel.insert(id);
    m_project.setSelection(sel);
    if (wasEmpty) m_timeline->zoomToFit();
    m_program->seek(m_program->position());
}

void MainWindow::deleteSelection(bool ripple) {
    // The selection already contains the linked partners when "linked selection" is on; Alt+click selects a
    // single clip on purpose (e.g. only the sound of a video), so delete exactly what is selected.
    const QSet<QString> ids = m_project.selection();
    if (ids.isEmpty()) return;
    m_project.edit(ripple ? "Ripple delete" : "Delete", [&](Timeline& tl) { ops::remove(tl, ids, ripple); });
}

void MainWindow::splitAtPlayhead(bool allTracks) {
    const double t = snapToFrame(m_program->position(), m_project.timeline().fps);
    // Like Premiere: cut the selected clips if they are under the playhead, otherwise every track.
    QSet<QString> sel;
    if (!allTracks)
        for (const QString& id : m_project.selection())
            if (const Clip* c = m_project.timeline().clip(id))
                if (t > c->start + 1e-6 && t < c->end() - 1e-6) sel.insert(id);
    const bool everything = allTracks || sel.isEmpty();

    Timeline probe = m_project.timeline();
    bool any = false;
    if (everything) any = ops::splitAllAt(probe, t) > 0;
    else for (const QString& id : sel) any = ops::splitAt(probe, id, t) || any;
    if (!any) {
        setStatus("Nothing to split at the playhead");
        return;
    }
    m_project.edit("Add edit", [&](Timeline& tl) {
        if (everything) ops::splitAllAt(tl, t);
        else for (const QString& id : sel) ops::splitAt(tl, id, t);
    });
}

void MainWindow::copySelection(bool cut) {
    const QSet<QString> ids = m_project.selection();
    if (ids.isEmpty()) return;
    m_clipboard.clear();
    const Timeline& tl = m_project.timeline();
    for (const QString& id : ids) {
        const ClipRef r = tl.find(id);
        if (!r.valid()) continue;
        m_clipboard.push_back({*tl.clip(r), r.type, r.track});
    }
    setStatus(QString("Copied %1 clip%2").arg(m_clipboard.size()).arg(m_clipboard.size() == 1 ? "" : "s"));
    if (cut) deleteSelection(false);
}

void MainWindow::paste() {
    if (m_clipboard.isEmpty()) return;
    double base = 1e18;
    for (const ClipCopy& c : m_clipboard) base = std::min(base, c.clip.start);
    const double at = snapToFrame(m_program->position(), m_project.timeline().fps);
    QHash<QString, QString> linkMap;
    QSet<QString> added;
    m_project.edit("Paste", [&](Timeline& tl) {
        for (const ClipCopy& cc : m_clipboard) {
            Clip c = cc.clip;
            c.id = newId();
            c.start = at + (cc.clip.start - base);
            if (!c.linkId.isEmpty()) {
                if (!linkMap.contains(c.linkId)) linkMap.insert(c.linkId, newId());
                c.linkId = linkMap.value(c.linkId);
            }
            auto& tracks = tl.tracks(cc.type);
            const int ti = std::min(cc.track, int(tracks.size()) - 1);
            if (ti < 0 || tracks[ti].locked) continue;
            ops::place(tracks[ti], c);
            added.insert(c.id);
        }
    });
    m_project.setSelection(added);
}

void MainWindow::selectAll() {
    QSet<QString> ids;
    for (const auto* tracks : {&m_project.timeline().video, &m_project.timeline().audio})
        for (const Track& tr : *tracks)
            for (const Clip& c : tr.clips) ids.insert(c.id);
    m_project.setSelection(ids);
}

void MainWindow::toggleLink() {
    const QSet<QString> ids = m_project.selection();
    if (ids.size() < 2) {
        // a single clip: unlink it from its partner
        if (ids.size() == 1) {
            m_project.edit("Unlink", [&](Timeline& tl) {
                const QSet<QString> group = ops::linkedIds(tl, *ids.begin());
                if (group.size() < 2) return;
                for (const QString& id : group) if (Clip* c = tl.clip(id)) c->linkId.clear();
            });
        }
        return;
    }
    bool allLinked = true;
    QString first;
    for (const QString& id : ids) {
        const Clip* c = m_project.timeline().clip(id);
        if (!c || c->linkId.isEmpty()) { allLinked = false; break; }
        if (first.isEmpty()) first = c->linkId;
        else if (first != c->linkId) { allLinked = false; break; }
    }
    m_project.edit(allLinked ? "Unlink" : "Link", [&](Timeline& tl) {
        const QString link = allLinked ? QString() : newId();
        for (const QString& id : ids) if (Clip* c = tl.clip(id)) c->linkId = link;
    });
}

void MainWindow::addMarker() {
    const double t = snapToFrame(m_program->position(), m_project.timeline().fps);
    m_project.edit("Add marker", [&](Timeline& tl) {
        Marker m;
        m.time = t;
        m.name = QString("Marker %1").arg(tl.markers.size() + 1);
        tl.markers.push_back(m);
    });
}

void MainWindow::setMark(bool in) {
    const double t = snapToFrame(m_program->position(), m_project.timeline().fps);
    m_project.edit(in ? "Mark In" : "Mark Out", [&](Timeline& tl) {
        if (in) { tl.markIn = t; if (tl.markOut >= 0 && tl.markOut <= t) tl.markOut = -1; }
        else { tl.markOut = t; if (tl.markIn >= 0 && tl.markIn >= t) tl.markIn = -1; }
    });
}

void MainWindow::clearMarks() {
    m_project.edit("Clear In and Out", [](Timeline& tl) { tl.markIn = tl.markOut = -1; });
}

void MainWindow::addTitle(double time, int track) {
    const double t = snapToFrame(std::max(0.0, time), m_project.timeline().fps);
    Clip c = ops::makeTitle(t, 5.0, "New Title");
    m_project.edit("New title", [&](Timeline& tl) {
        const int ti = std::max(0, std::min(track, int(tl.video.size()) - 1));
        if (!tl.video[ti].locked) ops::place(tl.video[ti], c);
    });
    m_project.setSelection({c.id});
    m_dockEffectControls->show();
    m_dockEffectControls->raise();
    setTool(Tool::Selection);
}

void MainWindow::applyEffect(const QString& clipId, const QString& type) {
    const EffectDef* def = EffectRegistry::find(type);
    if (!def) return;
    QSet<QString> ids = m_project.selection();
    if (!ids.contains(clipId)) ids = ops::linkedIds(m_project.timeline(), clipId);
    int applied = 0;
    m_project.edit(QString("Apply %1").arg(def->name), [&](Timeline& tl) {
        for (const QString& id : ids) {
            Clip* c = tl.clip(id);
            if (!c) continue;
            const bool audioClip = c->mediaKind == MediaKind::Audio && c->kind == ClipKind::Media;
            if (audioClip == def->video) continue;  // video effects only on picture, audio effects only on sound
            if (c->effect(type)) continue;
            c->effects.push_back(EffectRegistry::make(type));
            ++applied;
        }
    });
    if (applied == 0) {
        setStatus(def->video ? "That effect can only be applied to video clips" : "That effect can only be applied to audio clips");
        m_project.undoStack()->undo();
    } else {
        m_dockEffectControls->show();
        m_dockEffectControls->raise();
    }
}

void MainWindow::applyEffectToSelection(const QString& type) {
    const QString id = m_project.primarySelection();
    if (id.isEmpty()) {
        setStatus("Select a clip on the timeline first, then double-click the effect");
        return;
    }
    applyEffect(id, type);
}

void MainWindow::setFade(bool fadeIn) {
    const QSet<QString> ids = m_project.selection();
    if (ids.isEmpty()) return;
    m_project.edit(fadeIn ? "Fade in" : "Fade out", [&](Timeline& tl) {
        for (const QString& id : ids) {
            Clip* c = tl.clip(id);
            if (!c) continue;
            EffectInstance* e = c->effect("opacity");
            if (!e) e = c->effect("volume");
            if (e) e->set(fadeIn ? "fade_in" : "fade_out", std::min(1.0, c->duration / 2));
        }
    });
}

void MainWindow::toggleClipEnabled() {
    const QSet<QString> ids = m_project.selection();
    if (ids.isEmpty()) return;
    m_project.edit("Enable / disable clip", [&](Timeline& tl) {
        for (const QString& id : ids) if (Clip* c = tl.clip(id)) c->enabled = !c->enabled;
    });
}

void MainWindow::removeEffectsFromSelection() {
    const QSet<QString> ids = m_project.selection();
    if (ids.isEmpty()) return;
    m_project.edit("Remove all effects", [&](Timeline& tl) {
        for (const QString& id : ids)
            if (Clip* c = tl.clip(id))
                for (int i = c->effects.size() - 1; i >= 0; --i) {
                    const EffectDef* d = EffectRegistry::find(c->effects[i].type);
                    if (d && !d->intrinsic) c->effects.removeAt(i);
                }
    });
}

void MainWindow::sequenceSettings() {
    SequenceDialog dlg(m_project.timeline(), this);
    if (dlg.exec() == QDialog::Accepted) m_project.setSequenceSettings(dlg.width(), dlg.height(), dlg.fps());
}

void MainWindow::addTrack(TrackType t) {
    m_project.edit(t == TrackType::Video ? "Add video track" : "Add audio track", [&](Timeline& tl) { ops::addTrack(tl, t); });
}

void MainWindow::removeLastTrack(TrackType t) {
    const Timeline& tl = m_project.timeline();
    const int idx = int(tl.tracks(t).size()) - 1;
    if (idx < 1) return;
    if (!tl.tracks(t)[idx].clips.isEmpty()) {
        setStatus("The last track still has clips on it – remove them first");
        return;
    }
    m_project.edit("Delete track", [&](Timeline& timeline) { ops::removeTrack(timeline, t, idx); });
}

void MainWindow::showClipMenu(const QPoint& pos, const QString& clipId) {
    QMenu menu(this);
    const Clip* c = m_project.timeline().clip(clipId);
    if (!c) return;
    menu.addAction("Split at Playhead", QKeySequence("Ctrl+K"), [this] { splitAtPlayhead(false); });
    menu.addSeparator();
    menu.addAction("Delete", [this] { deleteSelection(false); });
    menu.addAction("Ripple Delete", [this] { deleteSelection(true); });
    menu.addSeparator();
    menu.addAction("Copy", [this] { copySelection(false); });
    menu.addAction("Cut", [this] { copySelection(true); });
    menu.addSeparator();
    const bool linked = !c->linkId.isEmpty() && ops::linkedIds(m_project.timeline(), clipId).size() > 1;
    menu.addAction(linked ? "Unlink" : "Link", [this] { toggleLink(); });
    menu.addAction(c->enabled ? "Disable Clip" : "Enable Clip", [this] { toggleClipEnabled(); });
    menu.addSeparator();
    menu.addAction("Fade In (1 s)", [this] { setFade(true); });
    menu.addAction("Fade Out (1 s)", [this] { setFade(false); });
    menu.addAction("Remove All Effects", [this] { removeEffectsFromSelection(); });
    menu.addSeparator();
    menu.addAction("Show in Effect Controls", [this] { m_dockEffectControls->show(); m_dockEffectControls->raise(); });
    menu.exec(pos);
}

}  // namespace hr
