#pragma once
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
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <atomic>

class QThread;

namespace hr {

// The AI Assistant: everything runs on this computer (no account, no keys).
//   * speech recognition (Whisper) -> sentence list, text based cutting, captions, SRT export
//   * silence remover (from the waveform)
//   * scene change detection
//   * Auto Edit: several of the above in one click
class AiPanel : public QScrollArea {
    Q_OBJECT
public:
    AiPanel(Project* project, MediaAssets* assets, Player* program, QWidget* parent = nullptr);
    ~AiPanel() override;

    bool isBusy() const { return m_busy; }
    int sentenceCount() const { return m_rows.size(); }

public slots:
    void transcribeSelection();                      // selected clips, or everything on the timeline without a transcript
    void removeSilences();
    void deleteSentences();                          // the selected rows
    void keepSelectedSentences();
    void addCaptions(bool selectedOnly = true);
    void exportSrt();
    void detectScenes(bool cut);                     // false: markers, true: cuts
    void autoEdit(bool silences, bool captions);
    void selectSentences(const QList<int>& rows);    // (for scripting / tests)
    void selectMatching(const QString& text);

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
    void say(const QString& text);
    QVector<MediaItem> mediaToTranscribe(bool allMissing = false) const;
    void startTranscription(const QVector<MediaItem>& list);
    void startNextTranscription();
    void finishTranscriptions(bool ok, const QString& message);
    ai::ModelInfo chosenModel() const;
    QString chosenLanguage() const;
    QVector<ai::TimelineSegment> selectedSegments() const;
    QVector<TimeRange> findSilenceRanges(bool* incomplete = nullptr) const;
    void applyScenes();
    void finishAutoEdit();

    Project* m_project;
    MediaAssets* m_assets;
    Player* m_program;

    // speech
    QComboBox* m_lang = nullptr;
    QComboBox* m_accuracy = nullptr;
    QPushButton* m_transcribeBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QProgressBar* m_bar = nullptr;
    QLabel* m_status = nullptr;
    QTreeWidget* m_list = nullptr;
    QLineEdit* m_search = nullptr;
    QPushButton *m_deleteBtn = nullptr, *m_keepBtn = nullptr, *m_captionBtn = nullptr, *m_srtBtn = nullptr;
    QVector<ai::TimelineSegment> m_rows;

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

    // auto edit
    QCheckBox *m_autoSilence = nullptr, *m_autoCaptions = nullptr;
    QPushButton* m_autoBtn = nullptr;
    bool m_autoPending = false, m_autoDoSilence = false, m_autoDoCaptions = false;
    QString m_autoNote;
    int m_lastRow = -1;

    // state
    bool m_busy = false;
    ai::AiModels m_models;
    ai::Transcriber m_transcriber;
    QStringList m_queue;
    ai::ModelInfo m_model;
    QString m_language;
    bool m_downloadingVad = false;
};

}  // namespace hr
