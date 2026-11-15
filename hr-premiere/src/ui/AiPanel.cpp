#include "AiPanel.h"

#include "Theme.h"
#include "ai/Scenes.h"
#include "ai/Silence.h"
#include "core/EditOps.h"
#include "core/Util.h"
#include "engine/Exporter.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QSettings>
#include <QThread>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace hr {

static const struct { const char* name; const char* code; } kLanguages[] = {
    {"Auto-detect", "auto"}, {"Arabic", "ar"},   {"English", "en"},    {"French", "fr"},     {"Spanish", "es"},
    {"German", "de"},        {"Turkish", "tr"},  {"Persian", "fa"},    {"Urdu", "ur"},       {"Hindi", "hi"},
    {"Russian", "ru"},       {"Italian", "it"},  {"Portuguese", "pt"}, {"Indonesian", "id"}, {"Japanese", "ja"},
    {"Korean", "ko"},        {"Chinese", "zh"},
};

AiPanel::AiPanel(Project* project, MediaAssets* assets, Player* program, QWidget* parent)
    : QScrollArea(parent), m_project(project), m_assets(assets), m_program(program) {
    buildUi();

    m_previewTimer.setSingleShot(true);
    m_previewTimer.setInterval(250);
    connect(&m_previewTimer, &QTimer::timeout, this, &AiPanel::updateSilencePreview);

    connect(m_project, &Project::timelineChanged, this, [this] { refreshSentences(); m_previewTimer.start(); });
    connect(m_project, &Project::mediaChanged, this, [this] { refreshSentences(); });
    connect(m_project, &Project::projectReset, this, [this] { refreshSentences(); m_previewTimer.start(); });
    connect(m_assets, &MediaAssets::updated, this, [this] { m_previewTimer.start(); });
    connect(m_program, &Player::positionChanged, this, &AiPanel::onPosition);

    // model download -> (voice detector) -> transcription
    connect(&m_models, &ai::AiModels::progress, this, [this](qint64 r, qint64 t) {
        if (t <= 0) return;
        m_bar->setRange(0, 1000);
        m_bar->setValue(int(r * 1000 / t));
        m_bar->setFormat(QString("%1 / %2 MB").arg(r / 1048576).arg(t / 1048576));
    });
    connect(&m_models, &ai::AiModels::finished, this, [this](bool ok, const QString& msg) {
        if (m_downloadingVad) {        // the voice detector is optional
            m_downloadingVad = false;
            startNextTranscription();
            return;
        }
        if (!ok) {
            finishTranscriptions(false, msg);
            return;
        }
        if (!ai::AiModels::isInstalled(ai::AiModels::vadModel())) {
            m_downloadingVad = true;
            say("Downloading the voice detector (small)…");
            m_models.download(ai::AiModels::vadModel());
            return;
        }
        startNextTranscription();
    });
    connect(&m_transcriber, &ai::Transcriber::progress, this, [this](double f) {
        m_bar->setRange(0, 1000);
        m_bar->setValue(int(f * 1000));
        m_bar->setFormat("%p%");
    });
    connect(&m_transcriber, &ai::Transcriber::finished, this,
            [this](bool ok, const QVector<TranscriptSegment>& segs, const QString& msg) {
                if (m_queue.isEmpty()) return;
                const QString id = m_queue.takeFirst();
                if (!ok) {
                    m_queue.clear();
                    finishTranscriptions(false, msg);
                    return;
                }
                m_project->setTranscript(id, segs, m_language, m_model.id);
                startNextTranscription();
            });
    refreshSentences();
    updateSilencePreview();
}

AiPanel::~AiPanel() {
    m_cancelScenes = true;
    if (m_sceneThread) {
        m_sceneThread->wait(5000);
    }
}

QFrame* AiPanel::card(const QString& title, QVBoxLayout** inner) {
    auto* f = new QFrame;
    f->setObjectName("card");
    auto* v = new QVBoxLayout(f);
    v->setContentsMargins(12, 10, 12, 12);
    v->setSpacing(8);
    auto* t = new QLabel(title);
    t->setStyleSheet(QString("font-weight:700; font-size:13px; color:%1; background:transparent;").arg(theme::accentHi.name()));
    v->addWidget(t);
    *inner = v;
    return f;
}

void AiPanel::buildUi() {
    setWidgetResizable(true);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(10, 10, 10, 10);
    v->setSpacing(10);

    auto* title = new QLabel("AI Assistant");
    title->setStyleSheet("font-size:17px; font-weight:800;");
    auto* sub = new QLabel("Runs on this computer: no account, no sign-in, no keys. "
                           "The speech model is downloaded once, the first time you use it.");
    sub->setWordWrap(true);
    sub->setObjectName("dim");
    v->addWidget(title);
    v->addWidget(sub);

    QSettings s;

    // ---- Auto Edit ---------------------------------------------------------------------------
    QVBoxLayout* a;
    v->addWidget(card("Auto Edit – one click", &a));
    m_autoSilence = new QCheckBox("Remove the silences");
    m_autoSilence->setChecked(true);
    m_autoCaptions = new QCheckBox("Add captions from what is said (listens to the speech first)");
    m_autoCaptions->setChecked(true);
    m_autoBtn = new QPushButton("Auto Edit the sequence");
    m_autoBtn->setObjectName("primary");
    a->addWidget(m_autoSilence);
    a->addWidget(m_autoCaptions);
    a->addWidget(m_autoBtn);
    connect(m_autoBtn, &QPushButton::clicked, this, [this] { autoEdit(m_autoSilence->isChecked(), m_autoCaptions->isChecked()); });

    // ---- Speech ----------------------------------------------------------------------------------
    QVBoxLayout* sp;
    v->addWidget(card("Speech – understand what is said", &sp));
    auto* row = new QGridLayout;
    row->setColumnStretch(1, 1);
    row->addWidget(new QLabel("Language"), 0, 0);
    m_lang = new QComboBox;
    for (const auto& l : kLanguages) m_lang->addItem(l.name, l.code);
    m_lang->setCurrentIndex(std::max(0, m_lang->findData(s.value("ai/language", "auto"))));
    row->addWidget(m_lang, 0, 1);
    row->addWidget(new QLabel("Accuracy"), 1, 0);
    m_accuracy = new QComboBox;
    for (const ai::ModelInfo& m : ai::AiModels::models()) m_accuracy->addItem(m.title, m.id);
    m_accuracy->setCurrentIndex(std::max(0, m_accuracy->findData(s.value("ai/model", "small"))));
    row->addWidget(m_accuracy, 1, 1);
    sp->addLayout(row);

    auto* btns = new QHBoxLayout;
    m_transcribeBtn = new QPushButton("Transcribe");
    m_transcribeBtn->setObjectName("primary");
    m_transcribeBtn->setToolTip("Listens to the selected clips (or to everything on the timeline) and writes down the sentences");
    m_cancelBtn = new QPushButton("Cancel");
    m_cancelBtn->setEnabled(false);
    btns->addWidget(m_transcribeBtn);
    btns->addWidget(m_cancelBtn);
    sp->addLayout(btns);
    m_bar = new QProgressBar;
    m_bar->setRange(0, 1000);
    m_bar->hide();
    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_status->setObjectName("dim");
    sp->addWidget(m_bar);
    sp->addWidget(m_status);

    m_list = new QTreeWidget;
    m_list->setHeaderLabels({"Time", "Sentence"});
    m_list->setRootIsDecorated(false);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setUniformRowHeights(false);
    m_list->setWordWrap(true);
    m_list->setMinimumHeight(190);
    m_list->header()->setStretchLastSection(true);
    m_list->setColumnWidth(0, 64);
    sp->addWidget(m_list);
    m_search = new QLineEdit;
    m_search->setPlaceholderText("Find a word – selects every sentence that contains it");
    m_search->setClearButtonEnabled(true);
    sp->addWidget(m_search);
    auto* edit = new QGridLayout;
    m_deleteBtn = new QPushButton("Delete selected");
    m_deleteBtn->setToolTip("Cuts the selected sentences out of the video, picture and sound together");
    m_keepBtn = new QPushButton("Keep only selected");
    m_keepBtn->setToolTip("Removes everything except the selected sentences");
    m_captionBtn = new QPushButton("Add captions");
    m_captionBtn->setToolTip("Puts the sentences on the picture as titles (selected ones, or all if nothing is selected)");
    m_srtBtn = new QPushButton("Export SRT…");
    edit->addWidget(m_deleteBtn, 0, 0);
    edit->addWidget(m_keepBtn, 0, 1);
    edit->addWidget(m_captionBtn, 1, 0);
    edit->addWidget(m_srtBtn, 1, 1);
    sp->addLayout(edit);

    connect(m_transcribeBtn, &QPushButton::clicked, this, &AiPanel::transcribeSelection);
    connect(m_cancelBtn, &QPushButton::clicked, this, [this] {
        m_cancelScenes = true;
        m_transcriber.cancel();
        m_models.cancel();
        m_queue.clear();
        m_autoPending = false;
        say("Cancelled.");
        setBusy(false);
    });
    connect(m_list, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* it) {
        const int i = it->data(0, Qt::UserRole).toInt();
        if (i >= 0 && i < m_rows.size()) m_program->seek(m_rows[i].start);
    });
    connect(m_search, &QLineEdit::textChanged, this, &AiPanel::selectMatching);
    connect(m_deleteBtn, &QPushButton::clicked, this, &AiPanel::deleteSentences);
    connect(m_keepBtn, &QPushButton::clicked, this, &AiPanel::keepSelectedSentences);
    connect(m_captionBtn, &QPushButton::clicked, this, [this] { addCaptions(true); });
    connect(m_srtBtn, &QPushButton::clicked, this, &AiPanel::exportSrt);

    // ---- Silence ----------------------------------------------------------------------------------
    QVBoxLayout* si;
    v->addWidget(card("Silence remover", &si));
    auto* g = new QGridLayout;
    g->setColumnStretch(1, 1);
    g->addWidget(new QLabel("Quieter than"), 0, 0);
    m_threshold = new QSlider(Qt::Horizontal);
    m_threshold->setRange(-60, -20);
    m_threshold->setValue(s.value("ai/silenceDb", -40).toInt());
    m_thresholdLabel = new QLabel;
    m_thresholdLabel->setMinimumWidth(52);
    auto* trow = new QHBoxLayout;
    trow->addWidget(m_threshold, 1);
    trow->addWidget(m_thresholdLabel);
    g->addLayout(trow, 0, 1);
    g->addWidget(new QLabel("Shortest silence"), 1, 0);
    m_minSilence = new QDoubleSpinBox;
    m_minSilence->setRange(0.2, 5.0);
    m_minSilence->setSingleStep(0.1);
    m_minSilence->setSuffix(" s");
    m_minSilence->setValue(0.6);
    g->addWidget(m_minSilence, 1, 1);
    g->addWidget(new QLabel("Keep around speech"), 2, 0);
    m_padding = new QDoubleSpinBox;
    m_padding->setRange(0.0, 1.0);
    m_padding->setSingleStep(0.05);
    m_padding->setSuffix(" s");
    m_padding->setValue(0.12);
    g->addWidget(m_padding, 2, 1);
    si->addLayout(g);
    m_silenceInfo = new QLabel;
    m_silenceInfo->setObjectName("dim");
    m_silenceInfo->setWordWrap(true);
    m_silenceBtn = new QPushButton("Remove silences");
    si->addWidget(m_silenceInfo);
    si->addWidget(m_silenceBtn);
    auto updateLabel = [this] { m_thresholdLabel->setText(QString("%1 dB").arg(m_threshold->value())); };
    updateLabel();
    connect(m_threshold, &QSlider::valueChanged, this, [this, updateLabel] {
        updateLabel();
        QSettings().setValue("ai/silenceDb", m_threshold->value());
        m_previewTimer.start();
    });
    connect(m_minSilence, &QDoubleSpinBox::valueChanged, this, [this] { m_previewTimer.start(); });
    connect(m_padding, &QDoubleSpinBox::valueChanged, this, [this] { m_previewTimer.start(); });
    connect(m_silenceBtn, &QPushButton::clicked, this, &AiPanel::removeSilences);

    // ---- Scenes -------------------------------------------------------------------------------------
    QVBoxLayout* sc;
    v->addWidget(card("Scene changes", &sc));
    auto* srow = new QHBoxLayout;
    srow->addWidget(new QLabel("Sensitivity"));
    m_sensitivity = new QSlider(Qt::Horizontal);
    m_sensitivity->setRange(0, 100);
    m_sensitivity->setValue(50);
    srow->addWidget(m_sensitivity, 1);
    sc->addLayout(srow);
    auto* sbtns = new QHBoxLayout;
    m_markBtn = new QPushButton("Add markers");
    m_cutBtn = new QPushButton("Cut at changes");
    sbtns->addWidget(m_markBtn);
    sbtns->addWidget(m_cutBtn);
    sc->addLayout(sbtns);
    connect(m_markBtn, &QPushButton::clicked, this, [this] { detectScenes(false); });
    connect(m_cutBtn, &QPushButton::clicked, this, [this] { detectScenes(true); });

    v->addStretch();
    setWidget(body);
}

// ------------------------------------------------------------------------------------------ helpers
void AiPanel::say(const QString& text) {
    m_status->setText(text);
    emit statusMessage(text);
}

void AiPanel::setBusy(bool busy, const QString& text) {
    m_busy = busy;
    for (QPushButton* b : {m_transcribeBtn, m_autoBtn, m_silenceBtn, m_markBtn, m_cutBtn, m_deleteBtn, m_keepBtn, m_captionBtn, m_srtBtn})
        b->setEnabled(!busy);
    m_cancelBtn->setEnabled(busy);
    m_bar->setVisible(busy);
    if (busy) {
        m_bar->setRange(0, 0);   // busy indicator until real progress arrives
        m_bar->setFormat("");
    }
    if (!text.isEmpty()) say(text);
}

ai::ModelInfo AiPanel::chosenModel() const { return ai::AiModels::modelById(m_accuracy->currentData().toString()); }
QString AiPanel::chosenLanguage() const { return m_lang->currentData().toString(); }

// ------------------------------------------------------------------------------------------ speech
QVector<MediaItem> AiPanel::mediaToTranscribe(bool allMissing) const {
    QVector<MediaItem> out;
    QSet<QString> seen;
    const Timeline& tl = m_project->timeline();
    const QSet<QString> sel = allMissing ? QSet<QString>() : m_project->selection();
    auto consider = [&](const Clip& c, bool force) {
        if (c.kind != ClipKind::Media || c.mediaId.isEmpty() || seen.contains(c.mediaId)) return;
        const MediaItem* m = m_project->mediaById(c.mediaId);
        if (!m || m->offline || !m->hasAudio) return;
        if (!force && !m->transcript.isEmpty()) return;     // already done
        seen.insert(m->id);
        out.push_back(*m);
    };
    for (const auto* tracks : {&tl.video, &tl.audio})
        for (const Track& tr : *tracks)
            for (const Clip& c : tr.clips) {
                if (!sel.isEmpty()) { if (sel.contains(c.id)) consider(c, true); }
                else consider(c, false);
            }
    return out;
}

void AiPanel::transcribeSelection() { startTranscription(mediaToTranscribe()); }

void AiPanel::startTranscription(const QVector<MediaItem>& list) {
    if (m_busy) return;
    if (list.isEmpty()) {
        say(m_project->selection().isEmpty()
                ? "Everything with sound on the timeline already has a transcript (select a clip and press Transcribe to do it again)."
                : "The selected clips have no sound to listen to.");
        if (m_autoPending) finishAutoEdit();
        return;
    }
    QString why;
    if (!ai::Transcriber::supportsWhisper(Exporter::findFfmpeg(), &why)) {
        say(why);
        if (m_autoPending) {          // still do the parts that do not need speech recognition
            m_autoNote = "Captions skipped: " + why;
            m_autoDoCaptions = false;
            finishAutoEdit();
        }
        return;
    }
    m_model = chosenModel();
    m_language = chosenLanguage();
    QSettings s;
    s.setValue("ai/model", m_model.id);
    s.setValue("ai/language", m_language);
    m_queue.clear();
    for (const MediaItem& m : list) m_queue << m.id;
    setBusy(true);
    if (!ai::AiModels::isInstalled(m_model)) {
        say(QString("Downloading the speech model \"%1\" – only the first time…").arg(m_model.title));
        m_models.download(m_model);
    } else if (!ai::AiModels::isInstalled(ai::AiModels::vadModel())) {
        m_downloadingVad = true;
        say("Downloading the voice detector (small)…");
        m_models.download(ai::AiModels::vadModel());
    } else {
        startNextTranscription();
    }
}

void AiPanel::startNextTranscription() {
    while (!m_queue.isEmpty() && !m_project->mediaById(m_queue.first())) m_queue.removeFirst();
    if (m_queue.isEmpty()) {
        finishTranscriptions(true, QString());
        return;
    }
    const MediaItem* m = m_project->mediaById(m_queue.first());
    say(QString("Listening to \"%1\"…  (this can take a while for long videos)").arg(m->name));
    m_bar->setRange(0, 1000);
    m_bar->setValue(0);
    m_bar->setFormat("%p%");
    ai::Transcriber::Request r;
    r.mediaId = m->id;
    r.mediaPath = m->path;
    r.duration = m->duration;
    r.language = m_language;
    r.model = m_model;
    m_transcriber.start(r);
}

void AiPanel::finishTranscriptions(bool ok, const QString& message) {
    setBusy(false);
    refreshSentences();
    if (!ok) {
        const QString why = message.isEmpty() ? "Speech recognition failed." : message;
        say(why);
        if (m_autoPending) {          // still do the parts that do not need speech recognition
            m_autoNote = "Captions skipped: " + why;
            m_autoDoCaptions = false;
            finishAutoEdit();
        }
        return;
    }
    say(m_rows.isEmpty() ? "No speech was found in the sound." : QString("Done: %1 sentences found. Click one to jump to it.").arg(m_rows.size()));
    if (m_autoPending) finishAutoEdit();
}

void AiPanel::refreshSentences() {
    m_rows = ai::transcriptOnTimeline(m_project->timeline(), m_project->media());
    m_list->clear();
    const double fps = m_project->timeline().fps;
    for (int i = 0; i < m_rows.size(); ++i) {
        const ai::TimelineSegment& r = m_rows[i];
        const qint64 secs = qint64(r.start);
        auto* it = new QTreeWidgetItem({QString("%1:%2").arg(secs / 60).arg(secs % 60, 2, 10, QLatin1Char('0')), r.text});
        it->setData(0, Qt::UserRole, i);
        it->setToolTip(1, QString("%1 – %2").arg(formatTimecode(r.start, fps), formatTimecode(r.end, fps)));
        m_list->addTopLevelItem(it);
    }
    const bool has = !m_rows.isEmpty() && !m_busy;
    for (QPushButton* b : {m_deleteBtn, m_keepBtn, m_captionBtn, m_srtBtn}) b->setEnabled(has);
}

QVector<ai::TimelineSegment> AiPanel::selectedSegments() const {
    QVector<ai::TimelineSegment> out;
    for (QTreeWidgetItem* it : m_list->selectedItems()) {
        const int i = it->data(0, Qt::UserRole).toInt();
        if (i >= 0 && i < m_rows.size()) out.push_back(m_rows[i]);
    }
    std::sort(out.begin(), out.end(), [](const ai::TimelineSegment& a, const ai::TimelineSegment& b) { return a.start < b.start; });
    return out;
}

void AiPanel::selectSentences(const QList<int>& rows) {
    m_list->clearSelection();
    for (int r : rows)
        if (r >= 0 && r < m_list->topLevelItemCount()) m_list->topLevelItem(r)->setSelected(true);
}

void AiPanel::selectMatching(const QString& text) {
    m_list->clearSelection();
    const QString q = text.trimmed();
    if (q.isEmpty()) return;
    QTreeWidgetItem* first = nullptr;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem* it = m_list->topLevelItem(i);
        if (it->text(1).contains(q, Qt::CaseInsensitive)) {
            it->setSelected(true);
            if (!first) first = it;
        }
    }
    if (first) m_list->scrollToItem(first);
}

void AiPanel::onPosition(double t) {
    int row = -1;
    for (int i = 0; i < m_rows.size(); ++i)
        if (t >= m_rows[i].start && t < m_rows[i].end) { row = i; break; }
    if (row == m_lastRow || row < 0 || row >= m_list->topLevelItemCount()) return;
    m_lastRow = row;
    if (m_program->isPlaying()) m_list->setCurrentItem(m_list->topLevelItem(row), 0, QItemSelectionModel::NoUpdate);
}

void AiPanel::deleteSentences() {
    const QVector<ai::TimelineSegment> sel = selectedSegments();
    if (sel.isEmpty()) {
        say("Select the sentences you want to remove first (or search for a word).");
        return;
    }
    QVector<TimeRange> ranges;
    for (const auto& s : sel) ranges.push_back({s.start, s.end});
    ranges = ai::mergeRanges(ranges, 0.05);
    double total = 0;
    for (const TimeRange& r : ranges) total += r.length();
    m_project->edit("Delete sentences", [&](Timeline& tl) { ops::cutRanges(tl, ranges, true); });
    say(QString("Removed %1 sentence%2 (%3 s).").arg(sel.size()).arg(sel.size() == 1 ? "" : "s").arg(total, 0, 'f', 1));
}

void AiPanel::keepSelectedSentences() {
    const QVector<ai::TimelineSegment> sel = selectedSegments();
    if (sel.isEmpty()) {
        say("Select the sentences you want to keep first.");
        return;
    }
    QVector<TimeRange> keep;
    for (const auto& s : sel) keep.push_back({s.start, s.end});
    const QVector<TimeRange> cuts = ai::complementRanges(keep, m_project->timeline().duration(), 0.1);
    double total = 0;
    for (const TimeRange& r : cuts) total += r.length();
    m_project->edit("Keep selected sentences", [&](Timeline& tl) { ops::cutRanges(tl, cuts, true); });
    say(QString("Kept %1 sentence%2; removed %3 s.").arg(sel.size()).arg(sel.size() == 1 ? "" : "s").arg(total, 0, 'f', 1));
}

void AiPanel::addCaptions(bool selectedOnly) {
    QVector<ai::TimelineSegment> segs = selectedOnly ? selectedSegments() : QVector<ai::TimelineSegment>();
    if (segs.isEmpty()) segs = m_rows;
    if (segs.isEmpty()) {
        say("Nothing to caption yet – press Transcribe first.");
        return;
    }
    const QVector<ai::TimelineSegment> parts = ai::splitForCaptions(segs, 56);
    int track = -1;
    m_project->edit("Add captions", [&](Timeline& tl) { track = ai::addCaptions(tl, parts); });
    say(QString("Added %1 captions on V%2. Select one to change the font, color or position in Effect Controls.").arg(parts.size()).arg(track + 1));
}

void AiPanel::exportSrt() {
    if (m_rows.isEmpty()) return;
    const QString path = QFileDialog::getSaveFileName(this, "Export subtitles", QDir::home().filePath(m_project->name() + ".srt"), "SubRip subtitles (*.srt)");
    if (path.isEmpty()) return;
    QVector<TranscriptSegment> segs;
    for (const auto& r : m_rows) segs.push_back({r.start, r.end, r.text});
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(ai::formatSrt(segs).toUtf8());
        say("Subtitles saved to " + path);
    } else {
        say("Could not save " + path);
    }
}

// ------------------------------------------------------------------------------------------ silence
QVector<TimeRange> AiPanel::findSilenceRanges(bool* incomplete) const {
    QHash<QString, QVector<quint8>> cache;
    auto provider = [&](const QString& id) -> const QVector<quint8>* {
        if (!cache.contains(id)) {
            const MediaAsset a = m_assets->get(id);
            cache.insert(id, a.wavesReady ? a.peaks100 : QVector<quint8>());
        }
        return &cache[id];
    };
    ai::SilenceOptions o;
    o.thresholdDb = m_threshold->value();
    o.minSilence = m_minSilence->value();
    o.padding = m_padding->value();
    return ai::findSilences(m_project->timeline(), provider, o, incomplete);
}

void AiPanel::updateSilencePreview() {
    bool incomplete = false;
    const QVector<TimeRange> r = findSilenceRanges(&incomplete);
    const double dur = m_project->timeline().duration();
    if (dur <= 0) {
        m_silenceInfo->setText("Add clips with sound to the timeline first.");
        return;
    }
    if (incomplete) {
        m_silenceInfo->setText("Still analysing the sound of some clips…");
        return;
    }
    double total = 0;
    for (const TimeRange& x : r) total += x.length();
    m_silenceInfo->setText(r.isEmpty() ? "No silences with these settings."
                                       : QString("%1 silence%2, %3 s of %4 s (%5 %) would be removed.")
                                             .arg(r.size()).arg(r.size() == 1 ? "" : "s").arg(total, 0, 'f', 1).arg(dur, 0, 'f', 1)
                                             .arg(int(std::lround(total / dur * 100))));
}

void AiPanel::removeSilences() {
    if (m_busy) return;
    bool incomplete = false;
    const QVector<TimeRange> ranges = findSilenceRanges(&incomplete);
    if (incomplete) {
        say("The sound of some clips is still being analysed – try again in a few seconds.");
        return;
    }
    if (ranges.isEmpty()) {
        say("No silences found with these settings.");
        return;
    }
    double total = 0;
    for (const TimeRange& r : ranges) total += r.length();
    m_project->edit("Remove silences", [&](Timeline& tl) { ops::cutRanges(tl, ranges, true); });
    say(QString("Removed %1 silence%2 (%3 s).").arg(ranges.size()).arg(ranges.size() == 1 ? "" : "s").arg(total, 0, 'f', 1));
}

// ------------------------------------------------------------------------------------------ scenes
void AiPanel::detectScenes(bool cut) {
    if (m_busy) return;
    // the media of the selected video clips, or of every video clip when nothing is selected
    QVector<MediaItem> list;
    QSet<QString> seen;
    const Timeline& tl = m_project->timeline();
    const QSet<QString> sel = m_project->selection();
    for (const Track& tr : tl.video)
        for (const Clip& c : tr.clips) {
            if (c.kind != ClipKind::Media || c.mediaKind != MediaKind::Video || seen.contains(c.mediaId)) continue;
            if (!sel.isEmpty() && !sel.contains(c.id)) continue;
            if (const MediaItem* m = m_project->mediaById(c.mediaId)) {
                if (m->offline) continue;
                seen.insert(m->id);
                list.push_back(*m);
            }
        }
    if (list.isEmpty()) {
        say("There is no video on the timeline to look at.");
        return;
    }
    m_sceneCut = cut;
    m_sceneResults.clear();
    m_cancelScenes = false;
    setBusy(true, "Looking for scene changes…");
    m_bar->setRange(0, 1000);
    ai::SceneOptions opt;
    opt.sensitivity = m_sensitivity->value() / 100.0;
    m_sceneThread = QThread::create([this, list, opt] {
        for (int i = 0; i < list.size() && !m_cancelScenes; ++i) {
            const QVector<double> cuts = ai::detectScenes(
                list[i].path, opt,
                [this, i, n = list.size()](double p) {
                    QMetaObject::invokeMethod(this, [this, f = (i + p) / n] { m_bar->setValue(int(f * 1000)); }, Qt::QueuedConnection);
                },
                &m_cancelScenes);
            QMetaObject::invokeMethod(this, [this, id = list[i].id, cuts] { m_sceneResults.insert(id, cuts); }, Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(this, [this] { applyScenes(); }, Qt::QueuedConnection);
    });
    connect(m_sceneThread, &QThread::finished, m_sceneThread, &QObject::deleteLater);
    connect(m_sceneThread, &QThread::finished, this, [this] { m_sceneThread = nullptr; });
    m_sceneThread->start();
}

void AiPanel::applyScenes() {
    if (!m_busy) return;                        // cancelled
    setBusy(false);
    // scene times are in media seconds: map them through the clips that show that media
    QVector<double> times;
    const Timeline& tl = m_project->timeline();
    const QSet<QString> sel = m_project->selection();
    for (const Track& tr : tl.video)
        for (const Clip& c : tr.clips) {
            if (c.kind != ClipKind::Media || !m_sceneResults.contains(c.mediaId)) continue;
            if (!sel.isEmpty() && !sel.contains(c.id)) continue;
            for (double t : m_sceneResults.value(c.mediaId))
                if (t > c.srcIn + 0.05 && t < c.srcIn + c.duration - 0.05) times.push_back(snapToFrame(c.start + (t - c.srcIn), tl.fps));
        }
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end(), [](double a, double b) { return std::abs(a - b) < 1e-6; }), times.end());
    if (times.isEmpty()) {
        say("No scene changes found. Try a higher sensitivity.");
        return;
    }
    if (m_sceneCut) {
        m_project->edit("Cut at scene changes", [&](Timeline& t) { for (double x : times) ops::splitAllAt(t, x); });
        say(QString("Cut the sequence at %1 scene change%2.").arg(times.size()).arg(times.size() == 1 ? "" : "s"));
    } else {
        m_project->edit("Mark scene changes", [&](Timeline& t) {
            for (double x : times) {
                Marker m;
                m.time = x;
                m.name = QString("Scene %1").arg(t.markers.size() + 1);
                m.color = QColor("#ffb454");
                t.markers.push_back(m);
            }
        });
        say(QString("Added %1 marker%2 at the scene changes.").arg(times.size()).arg(times.size() == 1 ? "" : "s"));
    }
}

// ------------------------------------------------------------------------------------------ auto edit
void AiPanel::autoEdit(bool silences, bool captions) {
    if (m_busy) return;
    if (!silences && !captions) {
        say("Tick at least one option.");
        return;
    }
    if (m_project->timeline().isEmpty()) {
        say("Add some clips to the timeline first.");
        return;
    }
    m_autoDoSilence = silences;
    m_autoDoCaptions = captions;
    m_autoPending = true;
    m_autoNote.clear();
    if (captions) {
        // listen to everything that has not been transcribed yet; finishTranscriptions() continues the job
        const QVector<MediaItem> missing = mediaToTranscribe(true);
        if (!missing.isEmpty()) {
            startTranscription(missing);
            return;
        }
    }
    finishAutoEdit();
}

void AiPanel::finishAutoEdit() {
    m_autoPending = false;
    QString summary;
    m_project->undoStack()->beginMacro("Auto Edit");
    if (m_autoDoSilence) {
        removeSilences();
        summary += m_status->text() + "  ";
    }
    if (m_autoDoCaptions) {
        refreshSentences();             // the sentences moved when silences were cut
        addCaptions(false);
        summary += m_status->text();
    }
    m_project->undoStack()->endMacro();
    say("Auto Edit done.  " + summary.trimmed() + (m_autoNote.isEmpty() ? QString() : "  " + m_autoNote));
    m_autoNote.clear();
}

}  // namespace hr
