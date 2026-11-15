#pragma once
#include "core/Model.h"
#include "engine/Exporter.h"
#include "ui/ParticleField.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QTimer>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>

namespace hr {

class ExportDialog : public QDialog {
    Q_OBJECT
public:
    ExportDialog(const Timeline& tl, const QString& suggestedName, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent*) override;
    void reject() override;

private slots:
    void browse();
    void startExport();
    void formatChanged();

private:
    void setBusy(bool busy);

    Timeline m_tl;
    QLineEdit* m_path;
    QComboBox* m_format;
    QComboBox* m_quality;
    QComboBox* m_res;
    QComboBox* m_fps;
    QComboBox* m_range;
    QLabel* m_info;
    QLabel* m_status;
    QProgressBar* m_bar;
    QPushButton* m_go;
    QPushButton* m_cancel;
    QPushButton* m_browse;
    Exporter* m_exporter = nullptr;
    QString m_dir;
};

class SequenceDialog : public QDialog {
    Q_OBJECT
public:
    SequenceDialog(const Timeline& tl, QWidget* parent = nullptr);
    int width() const { return m_w->value(); }
    int height() const { return m_h->value(); }
    double fps() const { return m_fps->currentData().toDouble(); }

private:
    QSpinBox *m_w, *m_h;
    QComboBox *m_preset, *m_fps;
};

// Clip > Speed/Duration (Ctrl+R)
class SpeedDialog : public QDialog {
public:
    SpeedDialog(double speedPercent, double durationSeconds, double fps, QWidget* parent = nullptr);
    double speed() const { return m_speed->value() / 100.0; }   // 1.0 = normal
    bool ripple() const { return m_ripple->isChecked(); }
    QString durationText() const { return m_dur->text(); }
    void setSpeedPercent(double p) { m_speed->setValue(p); }

private:
    QDoubleSpinBox* m_speed;
    QLineEdit* m_dur;
    QCheckBox* m_ripple;
    double m_span;   // seconds of footage the clip plays (kept when the speed changes)
    double m_fps;
};

class AboutDialog : public QDialog {
public:
    explicit AboutDialog(QWidget* parent = nullptr);
};

// "الملكية" – who owns the program (Kaylar) and who developed it (HR).
class OwnershipDialog : public QDialog {
public:
    explicit OwnershipDialog(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;

private:
    ParticleField m_field;
    QTimer m_timer;
    QElapsedTimer m_clock;
};

class ShortcutsDialog : public QDialog {
public:
    explicit ShortcutsDialog(QWidget* parent = nullptr);
};

}  // namespace hr
