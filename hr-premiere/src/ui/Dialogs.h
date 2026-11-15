#pragma once
#include "core/Model.h"
#include "engine/Exporter.h"

#include <QComboBox>
#include <QDialog>
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

class AboutDialog : public QDialog {
public:
    explicit AboutDialog(QWidget* parent = nullptr);
};

class ShortcutsDialog : public QDialog {
public:
    explicit ShortcutsDialog(QWidget* parent = nullptr);
};

}  // namespace hr
