#pragma once
#include "ChatView.h"
#include "ai/ChatParser.h"
#include "ai/Cleanup.h"
#include "ai/SmartEdit.h"
#include "ai/Transcript.h"
#include "ai/Whisper.h"
#include "core/Project.h"
#include "engine/Player.h"
#include "media/MediaAssets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <atomic>
#include <functional>

class QThread;

namespace hr {

// The AI Assistant: everything runs on this computer (no account, no keys).
//   * a chat that understands requests in Arabic or English ("remove the silences", "less zoom", "change the word ...")
//   * the full edit: dead air, shots, zoom movements, fades and captions, decided from what the footage is like
//   * speech recognition (Whisper) with clean-up of poor sound, a sentence list that can be corrected by hand, and
//     marks on the sentences that are probably wrong
//   * silence remover, scene change detection
class AiPanel : public QScrollArea {
    Q_OBJECT
public:
    AiPanel(Project* project, MediaAssets* assets, Player* program, QWidget* parent = nullptr);
    ~AiPanel() override;

    bool isBusy() const { return m_busy; }
    int sentenceCount() const { return m_rows.size(); }
    int doubtfulCount() const;
    ChatView* chat() const { return m_chat; }
    const ai::EditStyle& style() const { return m_style; }

public slots:
    void transcribeSelection();                      // selected clips, or everything on the timeline without a transcript
    void removeSilences();
    void deleteSentences();                          // the selected rows
    void keepSelectedSentences();
    void addCaptions(bool selectedOnly = true);
    void exportSrt();
    void detectScenes(bool cut);                     // false: markers, true: cuts
    void autoEdit(bool silences, bool captions);     // a simple edit: the dead air and / or the captions
    void smartEdit();                                // the full edit with the wishes in the panel
    void selectSentences(const QList<int>& rows);    // (for scripting / tests)
    void selectMatching(const QString& text);
    void sendChat(const QString& text);              // as if it was typed in the chat

signals:
    void statusMessage(const QString& text);

private slots:
    void refreshSentences();
    void updateSilencePreview();
    void onPosition(double t);

private:
    QFrame* card(const QString& title, QVBoxLayout** inner);
    void buildUi();
    void setBusy(bool busy, const QString& text = QString());
    void say(const QString& text);                   // status line (and the chat while it is working on a request)
    void bot(const QString& text);                   // a message of the assistant in the chat

    // speech
    QVector<MediaItem> mediaToTranscribe(bool allMissing = false) const;
    QVector<MediaItem> allSoundMedia() const;
    void startTranscription(const QVector<MediaItem>& list);
    void startNextTranscription();
    void finishTranscriptions(bool ok, const QString& message);
    ai::ModelInfo chosenModel() const;
    QString chosenLanguage() const;
    ai::Cleanup cleanupFor(const QString& mediaId) const;
    const QVector<quint8>* peaksOf(const QString& mediaId) const;
    QVector<ai::TimelineSegment> selectedSegments() const;
    QVector<TimeRange> findSilenceRanges(bool* incomplete = nullptr) const;
    ai::PeaksProvider provider() const;
    void applyScenes();
    void onSentenceEdited(int row, const QString& newText);   // a sentence of the list was typed over

    // the full edit
    void runSmart();
    void applySmart();
    void whenSoundIsAnalysed(const std::function<void()>& next);
    bool canRegenerate() const;
    void regenerate(const QString& why = QString());
    QString describe(const ai::VideoProfile& p, const ai::EditPlan& plan, const ai::EditResult& r) const;
    QString reasonText(const QString& key) const;
    void ask(const QString& question, const std::function<void()>& onYes);

    // the chat
    void handle(const ai::Command& c);
    void tweakStyle(const std::function<void(ai::EditStyle&)>& change, const QString& done);
    void captionStyle(const ai::Command& c);
    int rowFor(int sentence) const;
    QString helpText() const;
    QString summaryText();

    Project* m_project;
    MediaAssets* m_assets;
    Player* m_program;

    // chat
    ChatView* m_chat = nullptr;
    bool m_mirror = false;                           // messages of say() also go to the chat
    std::function<void()> m_pendingYes;

    // full edit
    QCheckBox *m_optSilence = nullptr, *m_optMotion = nullptr, *m_optCaptions = nullptr, *m_optFades = nullptr;
    QComboBox* m_pace = nullptr;
    QPushButton* m_autoBtn = nullptr;
    ai::EditStyle m_style;
    ai::EditPlan m_lastPlan;
    ai::VideoProfile m_lastProfile;
    bool m_haveBaseline = false;
    Timeline m_baseline;                             // the sequence as it was before the full edit
    int m_smartIndex = -1;                           // undo index right after the full edit
    std::function<void(bool)> m_afterTranscribe;     // what to do when the listening is finished
    QString m_smartNote;

    // speech
    QComboBox* m_lang = nullptr;
    QComboBox* m_accuracy = nullptr;
    QComboBox* m_clean = nullptr;
    QPushButton* m_transcribeBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QProgressBar* m_bar = nullptr;
    QLabel* m_status = nullptr;
    QTreeWidget* m_list = nullptr;
    QLineEdit* m_search = nullptr;
    QPushButton *m_deleteBtn = nullptr, *m_keepBtn = nullptr, *m_captionBtn = nullptr, *m_srtBtn = nullptr;
    QVector<ai::TimelineSegment> m_rows;
    QHash<QString, QVector<ai::SegmentQuality>> m_quality;
    bool m_rebuilding = false;
    bool m_editing = false;                          // the user is typing in a sentence

    // silence
    QSlider* m_threshold = nullptr;
    QLabel* m_thresholdLabel = nullptr;
    QDoubleSpinBox *m_minSilence = nullptr, *m_padding = nullptr;
    QLabel* m_silenceInfo = nullptr;
    QPushButton* m_silenceBtn = nullptr;
    QTimer m_previewTimer;

    // scenes
    QSlider* m_sensitivity = nullptr;
    QPushButton *m_markBtn = nullptr, *m_cutBtn = nullptr;
    QHash<QString, QVector<double>> m_sceneResults;
    std::atomic<bool> m_cancelScenes{false};
    bool m_sceneCut = false;
    QThread* m_sceneThread = nullptr;
    int m_lastRow = -1;

    // state
    bool m_busy = false;
    ai::AiModels m_models;
    ai::Transcriber m_transcriber;
    QStringList m_queue;
    ai::ModelInfo m_model;
    QString m_language;
    bool m_downloadingVad = false;
    int m_inventedRemoved = 0;
    mutable QMap<QString, QVector<quint8>> m_peaksCache;     // waveforms handed to the analysis (stable addresses)
};

}  // namespace hr
