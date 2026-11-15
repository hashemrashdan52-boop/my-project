#include "AiPanel.h"

#include "Theme.h"
#include "ai/Scenes.h"
#include "ai/Silence.h"
#include "core/EditOps.h"
#include "core/I18n.h"
#include "core/TextUtil.h"
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
#include <memory>

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
    connect(m_project, &Project::projectReset, this, [this] {
        m_haveBaseline = false;
        m_smartIndex = -1;
        m_peaksCache.clear();
        refreshSentences();
        m_previewTimer.start();
    });
    connect(m_assets, &MediaAssets::updated, this, [this] { m_previewTimer.start(); });
    connect(m_program, &Player::positionChanged, this, &AiPanel::onPosition);
    connect(m_chat, &ChatView::sendRequested, this, &AiPanel::sendChat);

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
            say(TR("Downloading the voice detector (small)…"));
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
                int removed = 0;
                const QVector<TranscriptSegment> clean = ai::sanitizeTranscript(segs, peaksOf(id), &removed);   // what the model made up is dropped
                m_inventedRemoved += removed;
                m_project->setTranscript(id, clean, m_language, m_model.id);
                startNextTranscription();
            });
    refreshSentences();
    updateSilencePreview();

    bot(TR("Hi! I'm your editing assistant. I can cut the silences, add movement and captions, and fix the text. "
           "Tell me what you want, for example “full edit”. Say “help” to see everything I can do."));
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
    t->setObjectName("cardTitle");
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

    auto* title = new QLabel(TR("Editing Assistant"));
    title->setStyleSheet("font-size:17px; font-weight:800;");
    auto* sub = new QLabel(TR("Runs on this computer: no account, no sign-in, no keys. "
                              "The speech model is downloaded once, the first time you use it."));
    sub->setWordWrap(true);
    sub->setObjectName("dim");
    v->addWidget(title);
    v->addWidget(sub);

    QSettings s;

    // ---- Chat --------------------------------------------------------------------------------------
    QVBoxLayout* ch;
    v->addWidget(card(TR("Chat"), &ch));
    m_chat = new ChatView;
    const QStringList chips = {TR("Full edit"), TR("Remove silences"), TR("Add captions"), TR("Add zoom"), TR("Undo"), TR("Text mistakes?")};
    m_chat->setChips(chips, chips);          // the words on the buttons are valid requests in the language of the program
    ch->addWidget(m_chat);

    // ---- The full edit -----------------------------------------------------------------------------
    QVBoxLayout* a;
    v->addWidget(card(TR("Full edit – one click"), &a));
    auto* what = new QLabel(TR("I look at the video (the sound, the speech, how fast it is spoken) and decide what suits it."));
    what->setWordWrap(true);
    what->setObjectName("dim");
    a->addWidget(what);
    m_optSilence = new QCheckBox(TR("Cut the silences"));
    m_optMotion = new QCheckBox(TR("Add zoom and movement"));
    m_optCaptions = new QCheckBox(TR("Add captions from what is said"));
    m_optFades = new QCheckBox(TR("Fade in and out"));
    for (QCheckBox* c : {m_optSilence, m_optMotion, m_optCaptions, m_optFades}) {
        c->setChecked(true);
        a->addWidget(c);
    }
    auto* paceRow = new QHBoxLayout;
    paceRow->addWidget(new QLabel(TR("Style")));
    m_pace = new QComboBox;
    m_pace->addItem(TR("Balanced"), "balanced");
    m_pace->addItem(TR("Fast (TikTok, Reels)"), "fast");
    m_pace->addItem(TR("Calm (cinematic)"), "calm");
    paceRow->addWidget(m_pace, 1);
    a->addLayout(paceRow);
    m_autoBtn = new QPushButton(TR("Edit the whole video"));
    m_autoBtn->setObjectName("primary");
    a->addWidget(m_autoBtn);
    connect(m_autoBtn, &QPushButton::clicked, this, [this] {
        m_style.silence = m_optSilence->isChecked();
        m_style.motion = m_optMotion->isChecked();
        m_style.captions = m_optCaptions->isChecked();
        m_style.fades = m_optFades->isChecked();
        const QString p = m_pace->currentData().toString();
        m_style.pace = p == "fast" ? ai::Pace::Fast : (p == "calm" ? ai::Pace::Calm : ai::Pace::Balanced);
        m_mirror = true;
        runSmart();
    });

    // ---- Speech ----------------------------------------------------------------------------------
    QVBoxLayout* sp;
    v->addWidget(card(TR("Speech – understand what is said"), &sp));
    auto* row = new QGridLayout;
    row->setColumnStretch(1, 1);
    row->addWidget(new QLabel(TR("Language")), 0, 0);
    m_lang = new QComboBox;
    for (const auto& l : kLanguages) m_lang->addItem(TR(l.name), l.code);
    // until the user picks something else the speech is expected in the language of the program (people mostly record in
    // their own language, and a fixed language avoids the wrong guesses automatic detection makes on poor sound)
    m_lang->setCurrentIndex(std::max(0, m_lang->findData(s.value("ai/language", i18n::isRtl() ? "ar" : "auto"))));
    row->addWidget(m_lang, 0, 1);
    row->addWidget(new QLabel(TR("Accuracy")), 1, 0);
    m_accuracy = new QComboBox;
    for (const ai::ModelInfo& m : ai::AiModels::models()) m_accuracy->addItem(TR(m.title), m.id);
    m_accuracy->setCurrentIndex(std::max(0, m_accuracy->findData(s.value("ai/model", "small"))));
    row->addWidget(m_accuracy, 1, 1);
    row->addWidget(new QLabel(TR("Poor sound")), 2, 0);
    m_clean = new QComboBox;
    m_clean->addItem(TR("Clean up automatically (recommended)"), "auto");
    m_clean->addItem(TR("Leave the sound as it is"), "none");
    m_clean->addItem(TR("Level the sound (light)"), "light");
    m_clean->addItem(TR("Noisy recording (strong)"), "strong");
    m_clean->setCurrentIndex(std::max(0, m_clean->findData(s.value("ai/clean", "auto"))));
    m_clean->setToolTip(TR("The sound is cleaned up only while the program listens. Your file is never changed."));
    row->addWidget(m_clean, 2, 1);
    sp->addLayout(row);

    auto* btns = new QHBoxLayout;
    m_transcribeBtn = new QPushButton(TR("Transcribe"));
    m_transcribeBtn->setObjectName("primary");
    m_transcribeBtn->setToolTip(TR("Listens to the selected clips (or to everything on the timeline) and writes down the sentences"));
    m_cancelBtn = new QPushButton(TR("Cancel"));
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
    m_list->setHeaderLabels({QString(), TR("Time"), TR("Sentence")});
    m_list->setRootIsDecorated(false);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setUniformRowHeights(false);
    m_list->setWordWrap(true);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);     // a double click on the sentence starts editing (below)
    m_list->setMinimumHeight(190);
    m_list->header()->setStretchLastSection(true);
    m_list->setColumnWidth(0, 22);
    m_list->setColumnWidth(1, 56);
    m_list->setToolTip(TR("Click a sentence to hear it. Double-click to correct the words. ⚠ = may be wrong."));
    sp->addWidget(m_list);
    m_search = new QLineEdit;
    m_search->setPlaceholderText(TR("Find a word – selects every sentence that contains it"));
    m_search->setClearButtonEnabled(true);
    sp->addWidget(m_search);
    auto* edit = new QGridLayout;
    m_deleteBtn = new QPushButton(TR("Delete selected"));
    m_deleteBtn->setToolTip(TR("Cuts the selected sentences out of the video, picture and sound together"));
    m_keepBtn = new QPushButton(TR("Keep only selected"));
    m_keepBtn->setToolTip(TR("Removes everything except the selected sentences"));
    m_captionBtn = new QPushButton(TR("Add captions"));
    m_captionBtn->setToolTip(TR("Puts the sentences on the picture as titles (selected ones, or all if nothing is selected)"));
    m_srtBtn = new QPushButton(TR("Export SRT…"));
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
        m_afterTranscribe = nullptr;
        m_pendingYes = nullptr;
        say(TR("Cancelled."));
        setBusy(false);
    });
    connect(m_list, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* it) {
        const int i = it->data(0, Qt::UserRole).toInt();
        if (i >= 0 && i < m_rows.size()) m_program->seek(m_rows[i].start);
    });
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it, int column) {
        if (column != 2 || m_busy) return;
        it->setFlags(it->flags() | Qt::ItemIsEditable);
        m_editing = true;
        m_list->editItem(it, 2);
    });
    connect(m_list->itemDelegate(), &QAbstractItemDelegate::closeEditor, this, [this] { m_editing = false; });
    connect(m_list, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* it, int column) {
        if (m_rebuilding || column != 2) return;
        const int row = it->data(0, Qt::UserRole).toInt();
        const QString text = it->text(2);                                       // (the item itself is gone when the list is rebuilt)
        QTimer::singleShot(0, this, [this, row, text] { onSentenceEdited(row, text); });     // not inside the signal: the list is rebuilt
    });
    connect(m_search, &QLineEdit::textChanged, this, &AiPanel::selectMatching);
    connect(m_deleteBtn, &QPushButton::clicked, this, &AiPanel::deleteSentences);
    connect(m_keepBtn, &QPushButton::clicked, this, &AiPanel::keepSelectedSentences);
    connect(m_captionBtn, &QPushButton::clicked, this, [this] { addCaptions(true); });
    connect(m_srtBtn, &QPushButton::clicked, this, &AiPanel::exportSrt);

    // ---- Silence ----------------------------------------------------------------------------------
    QVBoxLayout* si;
    v->addWidget(card(TR("Silence remover"), &si));
    auto* g = new QGridLayout;
    g->setColumnStretch(1, 1);
    g->addWidget(new QLabel(TR("Quieter than")), 0, 0);
    m_threshold = new QSlider(Qt::Horizontal);
    m_threshold->setRange(-60, -20);
    m_threshold->setValue(s.value("ai/silenceDb", -40).toInt());
    m_thresholdLabel = new QLabel;
    m_thresholdLabel->setMinimumWidth(52);
    auto* trow = new QHBoxLayout;
    trow->addWidget(m_threshold, 1);
    trow->addWidget(m_thresholdLabel);
    g->addLayout(trow, 0, 1);
    g->addWidget(new QLabel(TR("Shortest silence")), 1, 0);
    m_minSilence = new QDoubleSpinBox;
    m_minSilence->setRange(0.2, 5.0);
    m_minSilence->setSingleStep(0.1);
    m_minSilence->setSuffix(TR(" s"));
    m_minSilence->setValue(0.6);
    g->addWidget(m_minSilence, 1, 1);
    g->addWidget(new QLabel(TR("Keep around speech")), 2, 0);
    m_padding = new QDoubleSpinBox;
    m_padding->setRange(0.0, 1.0);
    m_padding->setSingleStep(0.05);
    m_padding->setSuffix(TR(" s"));
    m_padding->setValue(0.12);
    g->addWidget(m_padding, 2, 1);
    si->addLayout(g);
    m_silenceInfo = new QLabel;
    m_silenceInfo->setObjectName("dim");
    m_silenceInfo->setWordWrap(true);
    m_silenceBtn = new QPushButton(TR("Remove silences"));
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
    v->addWidget(card(TR("Scene changes"), &sc));
    auto* srow = new QHBoxLayout;
    srow->addWidget(new QLabel(TR("Sensitivity")));
    m_sensitivity = new QSlider(Qt::Horizontal);
    m_sensitivity->setRange(0, 100);
    m_sensitivity->setValue(50);
    srow->addWidget(m_sensitivity, 1);
    sc->addLayout(srow);
    auto* sbtns = new QHBoxLayout;
    m_markBtn = new QPushButton(TR("Add markers"));
    m_cutBtn = new QPushButton(TR("Cut at changes"));
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
    if (m_mirror) bot(text);
}

void AiPanel::bot(const QString& text) { m_chat->addBot(text); }

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

const QVector<quint8>* AiPanel::peaksOf(const QString& mediaId) const {
    const MediaAsset a = m_assets->get(mediaId);
    if (!a.wavesReady || a.peaks100.isEmpty()) return nullptr;
    m_peaksCache.insert(mediaId, a.peaks100);
    return &m_peaksCache[mediaId];
}

ai::PeaksProvider AiPanel::provider() const {
    return [this](const QString& id) -> const QVector<quint8>* { return peaksOf(id); };
}

// How the program should clean the sound of one media file before it listens.
ai::Cleanup AiPanel::cleanupFor(const QString& mediaId) const {
    const QString choice = m_clean->currentData().toString();
    if (choice != "auto") return ai::cleanupFromName(choice, ai::Cleanup::Light);
    const QVector<quint8>* pk = peaksOf(mediaId);
    return pk ? ai::chooseCleanup(ai::measureAudio(*pk)) : ai::Cleanup::Light;
}

int AiPanel::doubtfulCount() const {
    int n = 0;
    for (const ai::TimelineSegment& r : m_rows) {
        const auto it = m_quality.constFind(r.mediaId);
        if (it != m_quality.constEnd() && r.index >= 0 && r.index < it->size() && (*it)[r.index].quality != ai::Quality::Good) ++n;
    }
    return n;
}

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

// Everything on the timeline that has sound, also what was already listened to (to listen again).
QVector<MediaItem> AiPanel::allSoundMedia() const {
    QVector<MediaItem> out;
    QSet<QString> seen;
    for (const auto* tracks : {&m_project->timeline().video, &m_project->timeline().audio})
        for (const Track& tr : *tracks)
            for (const Clip& c : tr.clips) {
                if (c.kind != ClipKind::Media || c.mediaId.isEmpty() || seen.contains(c.mediaId)) continue;
                const MediaItem* m = m_project->mediaById(c.mediaId);
                if (!m || m->offline || !m->hasAudio) continue;
                seen.insert(m->id);
                out.push_back(*m);
            }
    return out;
}

void AiPanel::transcribeSelection() { startTranscription(mediaToTranscribe()); }

void AiPanel::startTranscription(const QVector<MediaItem>& list) {
    if (m_busy) return;
    auto continueWith = [this](bool ok) {
        if (!m_afterTranscribe) return;
        auto f = std::move(m_afterTranscribe);
        m_afterTranscribe = nullptr;
        f(ok);
    };
    if (list.isEmpty()) {
        say(m_project->selection().isEmpty()
                ? TR("Everything with sound on the timeline already has a transcript (select a clip and press Transcribe to do it again).")
                : TR("The selected clips have no sound to listen to."));
        continueWith(true);
        return;
    }
    QString why;
    if (!ai::Transcriber::supportsWhisper(Exporter::findFfmpeg(), &why)) {
        say(why);
        continueWith(false);
        return;
    }
    m_model = chosenModel();
    m_language = chosenLanguage();
    m_inventedRemoved = 0;
    QSettings s;
    s.setValue("ai/model", m_model.id);
    s.setValue("ai/language", m_language);
    s.setValue("ai/clean", m_clean->currentData().toString());
    m_queue.clear();
    for (const MediaItem& m : list) m_queue << m.id;
    setBusy(true);
    if (!ai::AiModels::isInstalled(m_model)) {
        say(QString(TR("Downloading the speech model \"%1\" – only the first time…")).arg(TR(m_model.title)));
        m_models.download(m_model);
    } else if (!ai::AiModels::isInstalled(ai::AiModels::vadModel())) {
        m_downloadingVad = true;
        say(TR("Downloading the voice detector (small)…"));
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
    const ai::Cleanup level = cleanupFor(m->id);
    QString cleanText = level == ai::Cleanup::None ? TR("sound left as it is") : (level == ai::Cleanup::Light ? TR("sound levelled") : TR("noise removed"));
    say(QString(TR("Listening to \"%1\"… (%2; this can take a while for long videos)")).arg(m->name, cleanText));
    m_bar->setRange(0, 1000);
    m_bar->setValue(0);
    m_bar->setFormat("%p%");
    ai::Transcriber::Request r;
    r.mediaId = m->id;
    r.mediaPath = m->path;
    r.duration = m->duration;
    r.language = m_language;
    r.model = m_model;
    r.preFilter = ai::cleanupFilter(level);
    m_transcriber.start(r);
}

void AiPanel::finishTranscriptions(bool ok, const QString& message) {
    setBusy(false);
    refreshSentences();
    auto continueWith = [this](bool good) {
        if (m_afterTranscribe) {
            auto f = std::move(m_afterTranscribe);
            m_afterTranscribe = nullptr;
            f(good);
        }
    };
    if (!ok) {
        say(message.isEmpty() ? TR("Speech recognition failed.") : message);
        continueWith(false);
        if (!m_busy && !m_afterTranscribe && !m_pendingYes) m_mirror = false;
        return;
    }
    QString text = m_rows.isEmpty() ? TR("No speech was found in the sound.")
                                    : QString(TR("Done: %1 sentences found. Click one to jump to it.")).arg(m_rows.size());
    if (m_inventedRemoved > 0)
        text += "  " + QString(TR("I removed %1 sentences that looked invented from noise.")).arg(m_inventedRemoved);
    const int doubt = doubtfulCount();
    if (doubt > 0)
        text += "  " + QString(TR("%1 sentences are marked ⚠: the sound there is poor, so please check them.")).arg(doubt);
    say(text);
    continueWith(true);
    if (!m_busy && !m_afterTranscribe && !m_pendingYes) m_mirror = false;
}

void AiPanel::refreshSentences() {
    if (m_editing) return;                                              // the user is typing in a sentence
    m_rebuilding = true;
    m_rows = ai::transcriptOnTimeline(m_project->timeline(), m_project->media());
    // how reliable is each recognised sentence?
    m_quality.clear();
    for (const ai::TimelineSegment& r : m_rows)
        if (!m_quality.contains(r.mediaId))
            if (const MediaItem* m = m_project->mediaById(r.mediaId)) m_quality.insert(r.mediaId, ai::assessTranscript(m->transcript, peaksOf(r.mediaId)));
    m_list->clear();
    const double fps = m_project->timeline().fps;
    for (int i = 0; i < m_rows.size(); ++i) {
        const ai::TimelineSegment& r = m_rows[i];
        const qint64 secs = qint64(r.start);
        auto* it = new QTreeWidgetItem({QString(), QString("%1:%2").arg(secs / 60).arg(secs % 60, 2, 10, QLatin1Char('0')), r.text});
        it->setData(0, Qt::UserRole, i);
        const auto qi = m_quality.constFind(r.mediaId);
        ai::SegmentQuality q;
        if (qi != m_quality.constEnd() && r.index >= 0 && r.index < qi->size()) q = (*qi)[r.index];
        QString tip = QString("%1 – %2").arg(formatTimecode(r.start, fps), formatTimecode(r.end, fps));
        if (r.edited) {
            it->setText(0, "✎");
            tip += "\n" + TR("You corrected this sentence.");
        } else if (q.quality != ai::Quality::Good) {
            it->setText(0, "⚠");
            it->setForeground(0, QBrush(q.quality == ai::Quality::Poor ? theme::danger : QColor("#ffb454")));
            it->setForeground(2, QBrush(q.quality == ai::Quality::Poor ? theme::danger.lighter(125) : QColor("#ffb454")));
            QStringList why;
            for (const QString& k : q.reasons) why << "• " + reasonText(k);
            tip += "\n" + TR("This sentence may be wrong:") + "\n" + why.join("\n");
        }
        it->setToolTip(0, tip);
        it->setToolTip(2, tip);
        m_list->addTopLevelItem(it);
    }
    m_rebuilding = false;
    const bool has = !m_rows.isEmpty() && !m_busy;
    for (QPushButton* b : {m_deleteBtn, m_keepBtn, m_captionBtn, m_srtBtn}) b->setEnabled(has);
}

QString AiPanel::reasonText(const QString& key) const {
    if (key == "noisy") return TR("the sound is weak or full of noise here");
    if (key == "quiet") return TR("very quiet speech");
    if (key == "hallucination") return TR("this looks like text the model invents on noise");
    if (key == "repeated") return TR("repeated words or sentences");
    if (key == "too-fast") return TR("more letters than anyone can say in this time");
    if (key == "too-slow") return TR("very few words for this length");
    if (key == "tiny") return TR("a very short, quiet word");
    if (key == "talking") return TR("it is mostly someone talking");
    if (key == "fast-talker") return TR("the speaker talks fast, so even short pauses are cut");
    if (key == "slow-talker") return TR("the speaker talks slowly, so only long pauses are cut");
    if (key == "no-speech") return TR("no speech was recognised, so only the sound level was used");
    if (key == "vertical") return TR("the picture is vertical, so the captions are smaller and shorter");
    if (key == "calm") return TR("calm style: slow, gentle movements");
    if (key == "fast") return TR("fast style: quick, energetic movements");
    return key;
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
        if (containsTolerant(it->text(2), q)) {                 // ignores hamza, diacritics, ة / ه ...
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

// The new words of one sentence: the transcript and the captions on the picture change together (one undo step).
static void changeSentence(Project* project, const ai::TimelineSegment& row, const QString& text, int captionChars, const QString& undoText) {
    project->undoStack()->beginMacro(undoText);
    project->editSentence(row.mediaId, row.index, text);
    Timeline probe = project->timeline();
    if (ai::retextCaptions(probe, row.start, row.end, text, captionChars) > 0)
        project->edit(undoText, [&](Timeline& tl) { ai::retextCaptions(tl, row.start, row.end, text, captionChars); });
    project->undoStack()->endMacro();
}

void AiPanel::onSentenceEdited(int i, const QString& newText) {
    if (i < 0 || i >= m_rows.size()) return;
    const ai::TimelineSegment row = m_rows[i];
    const QString text = newText.simplified();
    if (text.isEmpty() || text == row.text) {
        refreshSentences();
        return;
    }
    changeSentence(m_project, row, text, std::max(20, m_lastPlan.captionChars), TR("Edit sentence"));
    say(TR("Sentence corrected (also in the captions). Ctrl+Z undoes it."));
}

int AiPanel::rowFor(int sentence) const {
    if (sentence == -1) return m_rows.isEmpty() ? -1 : int(m_rows.size()) - 1;
    return sentence >= 1 && sentence <= m_rows.size() ? sentence - 1 : -1;
}

void AiPanel::deleteSentences() {
    const QVector<ai::TimelineSegment> sel = selectedSegments();
    if (sel.isEmpty()) {
        say(TR("Select the sentences you want to remove first (or search for a word)."));
        return;
    }
    QVector<TimeRange> ranges;
    for (const auto& s : sel) ranges.push_back({s.start, s.end});
    ranges = ai::mergeRanges(ranges, 0.05);
    double total = 0;
    for (const TimeRange& r : ranges) total += r.length();
    m_project->edit(TR("Delete sentences"), [&](Timeline& tl) { ops::cutRanges(tl, ranges, true); });
    say(QString(TR("Removed %1 sentence(s) (%2 s).")).arg(sel.size()).arg(total, 0, 'f', 1));
}

void AiPanel::keepSelectedSentences() {
    const QVector<ai::TimelineSegment> sel = selectedSegments();
    if (sel.isEmpty()) {
        say(TR("Select the sentences you want to keep first."));
        return;
    }
    QVector<TimeRange> keep;
    for (const auto& s : sel) keep.push_back({s.start, s.end});
    const QVector<TimeRange> cuts = ai::complementRanges(keep, m_project->timeline().duration(), 0.1);
    double total = 0;
    for (const TimeRange& r : cuts) total += r.length();
    m_project->edit(TR("Keep selected sentences"), [&](Timeline& tl) { ops::cutRanges(tl, cuts, true); });
    say(QString(TR("Kept %1 sentence(s); removed %2 s.")).arg(sel.size()).arg(total, 0, 'f', 1));
}

void AiPanel::addCaptions(bool selectedOnly) {
    QVector<ai::TimelineSegment> segs = selectedOnly ? selectedSegments() : QVector<ai::TimelineSegment>();
    if (segs.isEmpty()) segs = m_rows;
    if (segs.isEmpty()) {
        say(TR("Nothing to caption yet – press Transcribe first."));
        return;
    }
    const bool all = segs.size() == m_rows.size();
    const ai::EditPlan plan = ai::planEdit(ai::analyzeProject(m_project->timeline(), m_project->media(), provider()), m_style);
    const QVector<ai::TimelineSegment> parts = ai::splitForCaptions(segs, plan.captionChars);
    int track = -1;
    m_project->edit(TR("Add captions"), [&](Timeline& tl) {
        if (all) ai::removeCaptions(tl);                      // adding captions twice never doubles them
        track = ai::addCaptions(tl, parts, plan.caption);
    });
    m_lastPlan = plan;
    say(QString(TR("Added %1 captions on V%2. Select one to change the font, color or position in Effect Controls.")).arg(parts.size()).arg(track + 1));
}

void AiPanel::exportSrt() {
    if (m_rows.isEmpty()) return;
    const QString path = QFileDialog::getSaveFileName(this, TR("Export subtitles"), QDir::home().filePath(m_project->name() + ".srt"), TR("SubRip subtitles (*.srt)"));
    if (path.isEmpty()) return;
    QVector<TranscriptSegment> segs;
    for (const auto& r : m_rows) segs.push_back({r.start, r.end, r.text});
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(ai::formatSrt(segs).toUtf8());
        say(TR("Subtitles saved to ") + path);
    } else {
        say(TR("Could not save ") + path);
    }
}

// ------------------------------------------------------------------------------------------ silence
QVector<TimeRange> AiPanel::findSilenceRanges(bool* incomplete) const {
    ai::SilenceOptions o;
    o.thresholdDb = m_threshold->value();
    o.minSilence = m_minSilence->value();
    o.padding = m_padding->value();
    return ai::findSilences(m_project->timeline(), provider(), o, incomplete);
}

void AiPanel::updateSilencePreview() {
    bool incomplete = false;
    const QVector<TimeRange> r = findSilenceRanges(&incomplete);
    const double dur = m_project->timeline().duration();
    if (dur <= 0) {
        m_silenceInfo->setText(TR("Add clips with sound to the timeline first."));
        return;
    }
    if (incomplete) {
        m_silenceInfo->setText(TR("Still analysing the sound of some clips…"));
        return;
    }
    double total = 0;
    for (const TimeRange& x : r) total += x.length();
    m_silenceInfo->setText(r.isEmpty() ? TR("No silences with these settings.")
                                       : QString(TR("%1 silence(s), %2 s of %3 s (%4 %) would be removed."))
                                             .arg(r.size()).arg(total, 0, 'f', 1).arg(dur, 0, 'f', 1).arg(int(std::lround(total / dur * 100))));
}

void AiPanel::removeSilences() {
    if (m_busy) return;
    bool incomplete = false;
    const QVector<TimeRange> ranges = findSilenceRanges(&incomplete);
    if (incomplete) {
        say(TR("The sound of some clips is still being analysed – try again in a few seconds."));
        return;
    }
    if (ranges.isEmpty()) {
        say(TR("No silences found with these settings."));
        return;
    }
    double total = 0;
    for (const TimeRange& r : ranges) total += r.length();
    m_project->edit(TR("Remove silences"), [&](Timeline& tl) { ops::cutRanges(tl, ranges, true); });
    say(QString(TR("Removed %1 silence(s) (%2 s).")).arg(ranges.size()).arg(total, 0, 'f', 1));
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
        say(TR("There is no video on the timeline to look at."));
        return;
    }
    m_sceneCut = cut;
    m_sceneResults.clear();
    m_cancelScenes = false;
    setBusy(true, TR("Looking for scene changes…"));
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
                if (t > c.srcIn + 0.05 && t < c.srcIn + c.duration * c.speed - 0.05)
                    times.push_back(snapToFrame(c.start + (t - c.srcIn) / c.speed, tl.fps));
        }
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end(), [](double a, double b) { return std::abs(a - b) < 1e-6; }), times.end());
    if (times.isEmpty()) {
        say(TR("No scene changes found. Try a higher sensitivity."));
        if (!m_afterTranscribe && !m_pendingYes) m_mirror = false;
        return;
    }
    if (m_sceneCut) {
        m_project->edit(TR("Cut at scene changes"), [&](Timeline& t) { for (double x : times) ops::splitAllAt(t, x); });
        say(QString(TR("Cut the sequence at %1 scene change(s).")).arg(times.size()));
    } else {
        m_project->edit(TR("Mark scene changes"), [&](Timeline& t) {
            for (double x : times) {
                Marker m;
                m.time = x;
                m.name = QString(TR("Scene %1")).arg(t.markers.size() + 1);
                m.color = QColor("#ffb454");
                t.markers.push_back(m);
            }
        });
        say(QString(TR("Added %1 marker(s) at the scene changes.")).arg(times.size()));
    }
    if (!m_afterTranscribe && !m_pendingYes) m_mirror = false;
}

// ------------------------------------------------------------------------------------------ the full edit
void AiPanel::autoEdit(bool silences, bool captions) {
    if (m_busy) return;
    if (!silences && !captions) {
        say(TR("Tick at least one option."));
        return;
    }
    if (m_project->timeline().isEmpty()) {
        say(TR("Add some clips to the timeline first."));
        return;
    }
    m_style = ai::EditStyle();
    m_style.silence = silences;
    m_style.captions = captions;
    m_style.motion = false;                 // this simple edit changes only the dead air and the captions
    m_style.fades = false;
    runSmart();
}

void AiPanel::smartEdit() { runSmart(); }

bool AiPanel::canRegenerate() const { return m_haveBaseline && m_project->undoStack()->index() == m_smartIndex; }

void AiPanel::whenSoundIsAnalysed(const std::function<void()>& next) {
    auto pending = [this] {
        for (const auto* tracks : {&m_project->timeline().audio})
            for (const Track& tr : *tracks)
                for (const Clip& c : tr.clips)
                    if (c.kind == ClipKind::Media && !c.mediaId.isEmpty() && !m_assets->get(c.mediaId).wavesReady) return true;
        return false;
    };
    if (!pending()) {
        next();
        return;
    }
    setBusy(true, TR("Analysing the sound…"));
    auto tries = std::make_shared<int>(0);
    auto poll = std::make_shared<std::function<void()>>();
    *poll = [this, pending, next, tries, poll] {
        if (!pending() || ++*tries > 240) {                  // up to about a minute, then work with what is known
            setBusy(false);
            next();
            return;
        }
        QTimer::singleShot(250, this, [poll] { (*poll)(); });
    };
    (*poll)();
}

void AiPanel::runSmart() {
    if (m_busy) {
        bot(TR("I'm still working on something – wait a moment or press Cancel."));
        return;
    }
    if (m_project->timeline().isEmpty()) {
        bot(TR("There is nothing on the timeline yet. Drag a video in first."));
        m_mirror = false;
        return;
    }
    m_smartNote.clear();
    // first the speech: the sentences tell where to cut and what to write
    const QVector<MediaItem> missing = mediaToTranscribe(true);
    if (!missing.isEmpty()) {
        QString why;
        if (ai::Transcriber::supportsWhisper(Exporter::findFfmpeg(), &why)) {
            bot(TR("First I listen to the speech, so I know where to cut and what to write. The first time it also downloads the speech model."));
            m_afterTranscribe = [this](bool ok) {
                if (!ok) m_smartNote = TR("I could not listen to the speech, so I worked only with the sound level and left out the captions.");
                whenSoundIsAnalysed([this] { applySmart(); });
            };
            startTranscription(missing);
            return;
        }
        m_smartNote = TR("Speech recognition is not available on this computer, so I worked only with the sound level and left out the captions.");
    }
    whenSoundIsAnalysed([this] { applySmart(); });
}

void AiPanel::applySmart() {
    const bool again = canRegenerate();
    const Timeline base = again ? m_baseline : m_project->timeline();     // always start from the untouched footage
    const QVector<MediaItem>& media = m_project->media();
    const ai::PeaksProvider peaks = provider();
    const ai::VideoProfile profile = ai::analyzeProject(base, media, peaks);
    const ai::EditPlan plan = ai::planEdit(profile, m_style);
    ai::EditResult r;
    m_project->edit(TR("AI edit"), [&](Timeline& tl) {
        tl = base;
        r = ai::applyPlan(tl, plan, media, peaks);
    });
    m_baseline = base;
    m_haveBaseline = true;
    m_smartIndex = m_project->undoStack()->index();
    m_lastPlan = plan;
    m_lastProfile = profile;
    refreshSentences();
    QString text = describe(profile, plan, r);
    if (!m_smartNote.isEmpty()) text += "\n\n" + m_smartNote;
    bot(text);
    say(TR("Full edit done."));
    m_mirror = false;
}

QString AiPanel::describe(const ai::VideoProfile& p, const ai::EditPlan& plan, const ai::EditResult& r) const {
    QStringList lines;
    lines << TR("Done! Here is what I did:");
    if (plan.style.silence) {
        if (r.silences > 0)
            lines << QString(TR("• I cut %1 pauses: %2 s of dead air (%3 % of the video).")).arg(r.silences).arg(r.silenceSeconds, 0, 'f', 1)
                         .arg(int(std::lround(r.silenceSeconds / std::max(0.1, r.oldDuration) * 100)));
        else
            lines << TR("• I found no pauses worth cutting.");
    }
    if (plan.style.motion)
        lines << QString(TR("• %1 shots, %2 of them with a zoom movement (%3 long takes were split at sentences).")).arg(r.shots).arg(r.zoomed).arg(r.splits);
    if (plan.style.captions)
        lines << (r.captions > 0 ? QString(TR("• I added %1 captions.")).arg(r.captions) : TR("• No captions: no speech was recognised."));
    if (plan.style.fades) lines << TR("• A fade in at the start and a fade out at the end.");
    if (r.keptLoudGaps > 0)
        lines << QString(TR("• %1 places have sound but no recognised words. I left them in case someone speaks there.")).arg(r.keptLoudGaps);
    lines << QString(TR("The video is now %1 s instead of %2 s.")).arg(r.newDuration, 0, 'f', 1).arg(r.oldDuration, 0, 'f', 1);
    QStringList why;
    for (const QString& k : plan.reasons) why << reasonText(k);
    if (!why.isEmpty()) lines << TR("Why: ") + why.join(TR("; ")) + ".";
    if (r.doubtful > 0)
        lines << QString(TR("⚠ %1 sentences may be wrong because the sound is poor or the text looks odd. They are marked in the list: say “text mistakes?” to see them.")).arg(r.doubtful);
    Q_UNUSED(p);
    lines << TR("Tell me if you want something different: “less zoom”, “no captions”, “cut pauses longer than 1 second”, or “undo”.");
    return lines.join("\n");
}

void AiPanel::regenerate(const QString& why) {
    Q_UNUSED(why);
    m_mirror = true;
    runSmart();
}

void AiPanel::ask(const QString& question, const std::function<void()>& onYes) {
    m_pendingYes = onYes;
    bot(question + "  " + TR("(yes / no)"));
}

// ------------------------------------------------------------------------------------------ the chat
void AiPanel::sendChat(const QString& text) {
    const QString t = text.trimmed();
    if (t.isEmpty()) return;
    m_chat->addUser(t);
    m_mirror = true;
    handle(ai::parseChat(t));
    if (!m_busy && !m_afterTranscribe && !m_pendingYes) m_mirror = false;
}

QString AiPanel::helpText() const {
    return TR("This is what I can do. Say it in your own words, in Arabic or English:\n"
              "• “full edit” – cut the silences, add zoom movements, fades and captions (add “fast” or “calm”, or “without captions”)\n"
              "• “remove the silences”, “cut pauses longer than 1 second”\n"
              "• “add captions”, “remove the captions”, “make the captions yellow / bigger / on top”\n"
              "• “add zoom”, “less zoom”, “more zoom”, “no zoom”, “add fades”\n"
              "• “speed up 2x”, “slow down”\n"
              "• “change the word X to Y”, “change sentence 3 to …”, “delete sentence 2”, “delete every sentence with the word …”\n"
              "• “text mistakes?” – I show the sentences that may be wrong; “listen again more accurately”; “the audio is noisy”\n"
              "• “cut at the scene changes”, “what is in the video?”, “undo”");
}

QString AiPanel::summaryText() {
    const Timeline& tl = m_project->timeline();
    if (tl.isEmpty()) return TR("The timeline is empty.");
    const ai::VideoProfile p = ai::analyzeProject(tl, m_project->media(), provider());
    QStringList lines;
    lines << QString(TR("The sequence is %1 s long with %2 shots.")).arg(p.duration, 0, 'f', 1).arg(p.shots);
    if (p.audio.valid)
        lines << QString(TR("Sound: speech about %1 dB, background noise about %2 dB.")).arg(p.audio.speechDb, 0, 'f', 0).arg(p.audio.noiseFloorDb, 0, 'f', 0);
    if (p.hasTranscript) {
        lines << QString(TR("I recognised %1 sentences (%2 % of the time somebody talks, about %3 words per second).")).arg(p.sentences).arg(int(std::lround(p.speechRatio * 100))).arg(p.wordsPerSecond, 0, 'f', 1);
        lines << QString(TR("%1 of them may be wrong (marked ⚠ in the list).")).arg(doubtfulCount());
    } else {
        lines << TR("I have not listened to the speech yet: say “transcribe”.");
    }
    bool incomplete = false;
    const QVector<TimeRange> sil = findSilenceRanges(&incomplete);
    double total = 0;
    for (const TimeRange& r : sil) total += r.length();
    if (!incomplete) lines << QString(TR("About %1 s of pauses could be cut with the settings of the silence remover.")).arg(total, 0, 'f', 1);
    return lines.join("\n");
}

void AiPanel::tweakStyle(const std::function<void(ai::EditStyle&)>& change, const QString& done) {
    change(m_style);
    m_optSilence->setChecked(m_style.silence);
    m_optMotion->setChecked(m_style.motion);
    m_optCaptions->setChecked(m_style.captions);
    m_optFades->setChecked(m_style.fades);
    if (canRegenerate()) {                          // my last edit is still the last thing in the sequence: redo it with the new wishes
        runSmart();
        return;
    }
    // otherwise change only the movement and the fades of the sequence as it is now
    const ai::EditPlan plan = ai::planEdit(ai::analyzeProject(m_project->timeline(), m_project->media(), provider()), m_style);
    m_project->edit(TR("AI changes"), [&](Timeline& tl) {
        if (m_style.motion) ai::applyMotion(tl, plan);
        else ai::clearMotion(tl);
        if (m_style.fades) ai::applyFades(tl, plan.fadeIn, plan.fadeOut);
        else ai::clearFades(tl);
    });
    bot(done);
}

void AiPanel::captionStyle(const ai::Command& c) {
    QString color = c.color;
    double size = 0, y = qQNaN();
    // the current look of the captions, to make them bigger / smaller than they are
    double current = m_style.captionSize > 0 ? m_style.captionSize : (m_lastPlan.caption.size > 0 ? m_lastPlan.caption.size : 54);
    for (const Track& tr : m_project->timeline().video)
        for (const Clip& cl : tr.clips)
            if (cl.role == "caption" && cl.effect("text")) { current = cl.effect("text")->num("size"); break; }
    if (c.bigger) size = std::min(300.0, current * 1.25);
    if (c.smaller) size = std::max(12.0, current * 0.8);
    if (c.position == "top") y = -36;
    else if (c.position == "bottom") y = 36;
    else if (c.position == "center") y = 0;
    if (!color.isEmpty()) m_style.captionColor = color;
    if (size > 0) m_style.captionSize = size;
    if (!std::isnan(y)) m_style.captionY = y;
    int n = 0;
    m_project->edit(TR("Change captions"), [&](Timeline& tl) { n = ai::restyleCaptions(tl, color, size, y); });
    if (n == 0) {
        m_project->undoStack()->undo();                 // nothing changed: do not leave an empty step behind
        bot(TR("There are no captions yet. Say “add captions” and I'll write them with this look."));
        return;
    }
    if (m_haveBaseline && !canRegenerate()) m_haveBaseline = false;
    bot(QString(TR("Done: I changed the look of %1 captions.")).arg(n));
}

void AiPanel::handle(const ai::Command& c) {
    using I = ai::Intent;
    auto syncBoxes = [this] {
        m_optSilence->setChecked(m_style.silence);
        m_optMotion->setChecked(m_style.motion);
        m_optCaptions->setChecked(m_style.captions);
        m_optFades->setChecked(m_style.fades);
    };
    switch (c.intent) {
    case I::Greeting: bot(TR("Hi! Tell me what you want me to do, for example “full edit” or “remove the silences”.")); return;
    case I::Thanks: bot(TR("You're welcome! If you want anything changed, just tell me.")); return;
    case I::Help: bot(helpText()); return;
    case I::HowToEditText:
        bot(TR("To correct the text: double-click a sentence in the list and type the right words, or tell me: “change the word X to Y” or “change sentence 3 to …”. "
               "The captions on the picture change too, and “undo” brings the old words back."));
        return;
    case I::Yes:
        if (m_pendingYes) {
            auto f = std::move(m_pendingYes);
            m_pendingYes = nullptr;
            f();
        } else {
            bot(TR("Yes to what? Tell me what you'd like me to do."));
        }
        return;
    case I::No:
        if (m_pendingYes) {
            m_pendingYes = nullptr;
            bot(TR("OK, I won't."));
        } else {
            bot(TR("OK."));
        }
        return;
    case I::Unknown:
        bot(TR("I didn't understand that. Try one of these:\n• full edit\n• remove the silences\n• add captions\n• less zoom\n• change the word … to …\n• undo\nSay “help” for the whole list."));
        return;
    case I::Undo: {
        QUndoStack* u = m_project->undoStack();
        if (!u->canUndo()) { bot(TR("There is nothing to undo.")); return; }
        const QString what = u->undoText();
        u->undo();
        bot(QString(TR("Undone: %1")).arg(what));
        return;
    }
    case I::Redo: {
        QUndoStack* u = m_project->undoStack();
        if (!u->canRedo()) { bot(TR("There is nothing to redo.")); return; }
        const QString what = u->redoText();
        u->redo();
        bot(QString(TR("Done again: %1")).arg(what));
        return;
    }
    default: break;
    }

    // everything below needs a sequence
    if (m_project->timeline().isEmpty() && c.intent != I::SetLanguage && c.intent != I::BetterAccuracy && c.intent != I::CleanAudio) {
        bot(TR("There is nothing on the timeline yet. Drag a video in first."));
        return;
    }
    if (m_busy) {
        bot(TR("I'm still working on something – wait a moment or press Cancel."));
        return;
    }

    switch (c.intent) {
    case I::FullEdit: {
        const QString captionLook = m_style.captionColor;
        const double size = m_style.captionSize, y = m_style.captionY;
        m_style = ai::EditStyle();
        m_style.captionColor = captionLook;
        m_style.captionSize = size;
        m_style.captionY = y;
        m_style.pace = c.preset == "fast" ? ai::Pace::Fast : (c.preset == "calm" ? ai::Pace::Calm : ai::Pace::Balanced);
        if (c.without.contains("captions")) m_style.captions = false;
        if (c.without.contains("motion")) m_style.motion = false;
        if (c.without.contains("fades")) m_style.fades = false;
        if (c.without.contains("silence")) m_style.silence = false;
        syncBoxes();
        m_pace->setCurrentIndex(std::max(0, m_pace->findData(c.preset.isEmpty() ? "balanced" : c.preset)));
        bot(TR("OK, I'm on it."));
        runSmart();
        return;
    }
    case I::RemoveSilence: {
        if (!std::isnan(c.seconds)) {
            m_style.minSilence = c.seconds;
            m_minSilence->setValue(std::clamp(c.seconds, 0.2, 5.0));
        }
        m_style.silence = true;
        syncBoxes();
        if (canRegenerate()) {
            runSmart();
        } else {
            removeSilences();
        }
        return;
    }
    case I::AddCaptions: {
        if (!c.color.isEmpty() || !c.position.isEmpty() || c.bigger || c.smaller) {
            // the look first (it is stored for the captions that are written next)
            if (!c.color.isEmpty()) m_style.captionColor = c.color;
            if (c.position == "top") m_style.captionY = -36;
            else if (c.position == "bottom") m_style.captionY = 36;
            else if (c.position == "center") m_style.captionY = 0;
            const double base = m_style.captionSize > 0 ? m_style.captionSize : 54;
            if (c.bigger) m_style.captionSize = std::min(300.0, base * 1.25);
            if (c.smaller) m_style.captionSize = std::max(12.0, base * 0.8);
        }
        m_style.captions = true;
        syncBoxes();
        if (canRegenerate()) {
            runSmart();
            return;
        }
        const QVector<MediaItem> missing = mediaToTranscribe(true);
        if (!missing.isEmpty()) {
            bot(TR("First I listen to the speech, so I can write it."));
            m_afterTranscribe = [this](bool ok) { if (ok) addCaptions(false); };
            startTranscription(missing);
        } else {
            addCaptions(false);
        }
        return;
    }
    case I::RemoveCaptions: {
        m_style.captions = false;
        syncBoxes();
        if (canRegenerate()) {
            runSmart();
            return;
        }
        int n = 0;
        Timeline probe = m_project->timeline();
        if (ai::removeCaptions(probe) > 0) m_project->edit(TR("Remove captions"), [&](Timeline& tl) { n = ai::removeCaptions(tl); });
        bot(n > 0 ? QString(TR("Done: I removed %1 captions.")).arg(n) : TR("There are no captions of mine on the picture."));
        return;
    }
    case I::CaptionStyle: captionStyle(c); return;
    case I::MotionOn: tweakStyle([](ai::EditStyle& s) { s.motion = true; }, TR("Done: I added zoom movements.")); return;
    case I::MotionOff: tweakStyle([](ai::EditStyle& s) { s.motion = false; }, TR("Done: I took the zoom movements away.")); return;
    case I::MotionMore: tweakStyle([](ai::EditStyle& s) { s.motion = true; s.motionStrength = std::min(2.0, s.motionStrength * 1.4); }, TR("Done: stronger zoom movements.")); return;
    case I::MotionLess: tweakStyle([](ai::EditStyle& s) { s.motion = true; s.motionStrength = std::max(0.2, s.motionStrength * 0.6); }, TR("Done: gentler zoom movements.")); return;
    case I::FadesOn: tweakStyle([](ai::EditStyle& s) { s.fades = true; }, TR("Done: fade in at the start and fade out at the end.")); return;
    case I::FadesOff: tweakStyle([](ai::EditStyle& s) { s.fades = false; }, TR("Done: no fades.")); return;
    case I::ChangeSpeed: {
        QSet<QString> ids = m_project->selection();
        if (ids.isEmpty())
            for (const auto* tracks : {&m_project->timeline().video, &m_project->timeline().audio})
                for (const Track& tr : *tracks)
                    for (const Clip& cl : tr.clips)
                        if (cl.kind == ClipKind::Media && cl.mediaKind != MediaKind::Image) ids.insert(cl.id);
        const double f = std::clamp(std::isnan(c.number) ? 2.0 : c.number, 0.05, 20.0);
        m_project->edit(TR("Speed / Duration"), [&](Timeline& tl) { ops::setSpeed(tl, ids, f, true); });
        bot(QString(TR("Done: the speed is now %1×.")).arg(f, 0, 'g', 3));
        return;
    }
    case I::Transcribe: m_mirror = true; transcribeSelection(); return;
    case I::BetterAccuracy: {
        static const QStringList order = {"tiny", "base", "small", "turbo"};
        const int now = std::max(0, int(order.indexOf(m_accuracy->currentData().toString())));
        const int next = std::min(int(order.size()) - 1, now + 1);
        m_accuracy->setCurrentIndex(std::max(0, m_accuracy->findData(order[next])));
        bool edited = false;
        for (const MediaItem& m : m_project->media())
            for (const TranscriptSegment& s : m.transcript) edited = edited || s.edited;
        auto go = [this] { m_mirror = true; startTranscription(allSoundMedia()); };
        if (next == now) bot(TR("The most accurate model is already selected. Poor sound is the next thing to improve: say “the audio is noisy”."));
        else if (edited) ask(TR("Listening again replaces the whole text, including the sentences you corrected by hand. Continue?"), go);
        else { bot(TR("I'll listen again with a more accurate model. It takes longer.")); go(); }
        return;
    }
    case I::CleanAudio: {
        m_clean->setCurrentIndex(std::max(0, m_clean->findData(c.level == "strong" ? "strong" : "light")));
        auto go = [this] { m_mirror = true; startTranscription(allSoundMedia()); };
        ask(QString(TR("I set the sound clean-up to “%1”. It only helps while I listen to the speech. Do you want me to listen again now?")).arg(m_clean->currentText()), go);
        return;
    }
    case I::SetLanguage: {
        m_lang->setCurrentIndex(std::max(0, m_lang->findData(c.language)));
        bot(QString(TR("OK, I will expect the speech in %1.")).arg(m_lang->currentText()));
        bool any = false;
        for (const MediaItem& m : m_project->media()) any = any || !m.transcript.isEmpty();
        if (any) ask(TR("Do you want me to listen again with this language?"), [this] { m_mirror = true; startTranscription(allSoundMedia()); });
        return;
    }
    case I::ReplaceWord: {
        m_project->undoStack()->beginMacro(TR("Replace word"));
        int n = m_project->replaceInTranscripts(c.from, c.to);
        Timeline probe = m_project->timeline();
        const int inCaptions = ai::replaceInCaptions(probe, c.from, c.to);
        if (inCaptions > 0) m_project->edit(TR("Replace word in captions"), [&](Timeline& tl) { ai::replaceInCaptions(tl, c.from, c.to); });
        m_project->undoStack()->endMacro();
        n = std::max(n, inCaptions);
        if (n == 0) bot(QString(TR("I couldn't find “%1”.")).arg(c.from));
        else bot(QString(TR("Done: “%1” is now “%2” (%3 places, also in the captions).")).arg(c.from, c.to).arg(n));
        return;
    }
    case I::EditSentence: {
        const int r = rowFor(c.sentence);
        if (r < 0) { bot(QString(TR("There is no sentence number %1.")).arg(c.sentence)); return; }
        changeSentence(m_project, m_rows[r], c.to, std::max(20, m_lastPlan.captionChars), TR("Edit sentence"));
        bot(QString(TR("Done: sentence %1 now says “%2”.")).arg(r + 1).arg(c.to));
        return;
    }
    case I::DeleteSentences:
    case I::KeepSentences: {
        const bool del = c.intent == I::DeleteSentences;
        if (c.sentence != 0) {
            const int r = rowFor(c.sentence);
            if (r < 0) { bot(QString(TR("There is no sentence number %1.")).arg(c.sentence)); return; }
            selectSentences({r});
        } else if (!c.from.isEmpty()) {
            selectMatching(c.from);
            if (m_list->selectedItems().isEmpty()) { bot(QString(TR("No sentence contains “%1”.")).arg(c.from)); return; }
        } else {
            bot(TR("Tell me which sentence: “delete sentence 2” or “delete every sentence with the word …”."));
            return;
        }
        if (del) deleteSentences();
        else keepSelectedSentences();
        return;
    }
    case I::ShowDoubtful: {
        QList<int> rows;
        for (int i = 0; i < m_rows.size(); ++i) {
            const auto it = m_quality.constFind(m_rows[i].mediaId);
            if (it != m_quality.constEnd() && m_rows[i].index >= 0 && m_rows[i].index < it->size() && (*it)[m_rows[i].index].quality != ai::Quality::Good) rows << i;
        }
        if (m_rows.isEmpty()) bot(TR("I have not listened to the speech yet: say “transcribe”."));
        else if (rows.isEmpty())
            bot(TR("I don't see sentences that look wrong. If you hear a mistake, tell me: “change the word X to Y”."));
        else {
            selectSentences(rows);
            bot(QString(TR("%1 sentences may be wrong. I selected them in the list (marked ⚠). Click one to hear it, double-click to correct it, or tell me what to change. "
                           "You can also say “listen again more accurately” or “the audio is noisy”.")).arg(rows.size()));
        }
        return;
    }
    case I::Summary: bot(summaryText()); return;
    case I::Scenes: m_mirror = true; detectScenes(c.cutScenes); return;
    case I::ExportSrt:
        if (m_rows.isEmpty()) bot(TR("There are no sentences yet: say “transcribe” first."));
        else exportSrt();
        return;
    default: break;
    }
}

}  // namespace hr
