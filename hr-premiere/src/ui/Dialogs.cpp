#include "core/I18n.h"
#include "Dialogs.h"

#include "Icons.h"
#include "Theme.h"
#include "core/Util.h"
#include "core/I18n.h"

#include <QCloseEvent>
#include <QColorDialog>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPainter>
#include <QSettings>
#include <QTableWidget>
#include <QHeaderView>
#include <QUrl>
#include <QVBoxLayout>
#include <cmath>

namespace hr {

// ===================================================================== Export
ExportDialog::ExportDialog(const Timeline& tl, const QString& suggestedName, QWidget* parent)
    : QDialog(parent), m_tl(tl) {
    setWindowTitle(TR("Export Media"));
    setMinimumWidth(520);
    QSettings s;
    m_dir = s.value("export/dir", QDir::homePath()).toString();

    m_path = new QLineEdit;
    m_browse = new QPushButton(TR("Browse…"));
    m_format = new QComboBox;
    m_format->addItem(TR("H.264 (MP4) – works everywhere"), "h264");
    m_format->addItem(TR("H.265 / HEVC (MP4) – smaller files"), "h265");
    m_format->addItem(TR("Apple ProRes 422 HQ (MOV)"), "prores");
    m_format->addItem(TR("VP9 (WebM)"), "vp9");
    m_format->addItem(TR("Animated GIF"), "gif");
    m_format->addItem(TR("Audio only – MP3"), "mp3");
    m_format->addItem(TR("Audio only – WAV"), "wav");
    m_quality = new QComboBox;
    m_quality->addItem(TR("High quality (larger file)"), 17);
    m_quality->addItem(TR("Medium"), 21);
    m_quality->addItem(TR("Low (smaller file)"), 27);
    m_quality->setCurrentIndex(1);
    m_res = new QComboBox;
    m_res->addItem(QString(TR("Match sequence (%1 × %2)")).arg(tl.width).arg(tl.height), 0);
    m_res->addItem("2160p  (4K)", 2160);
    m_res->addItem("1440p", 1440);
    m_res->addItem("1080p", 1080);
    m_res->addItem("720p", 720);
    m_res->addItem("480p", 480);
    m_fps = new QComboBox;
    m_fps->addItem(QString(TR("Match sequence (%1)")).arg(tl.fps, 0, 'f', tl.fps == std::floor(tl.fps) ? 0 : 3), 0.0);
    for (double f : {24.0, 25.0, 30.0, 50.0, 60.0}) m_fps->addItem(QString::number(f) + TR(" fps"), f);
    m_range = new QComboBox;
    m_range->addItem(TR("Entire sequence"), 0);
    if (tl.markIn >= 0 || tl.markOut >= 0) {
        m_range->addItem(TR("Work area (In to Out)"), 1);
        m_range->setCurrentIndex(1);
    }
    m_info = new QLabel;
    m_info->setObjectName("dim");
    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_bar = new QProgressBar;
    m_bar->setRange(0, 1000);
    m_bar->setValue(0);
    m_bar->setTextVisible(true);

    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(m_path, 1);
    pathRow->addWidget(m_browse);
    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(10);
    form->addRow(TR("Output file"), pathRow);
    form->addRow(TR("Format"), m_format);
    form->addRow(TR("Quality"), m_quality);
    form->addRow(TR("Resolution"), m_res);
    form->addRow(TR("Frame rate"), m_fps);
    form->addRow(TR("Range"), m_range);

    m_go = new QPushButton(TR("Export"));
    m_go->setObjectName("primary");
    m_go->setDefault(true);
    m_cancel = new QPushButton(TR("Close"));
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(m_cancel);
    buttons->addWidget(m_go);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(20, 18, 20, 18);
    lay->setSpacing(12);
    auto* title = new QLabel(TR("Export Settings"));
    title->setStyleSheet("font-size:16px; font-weight:700;");
    lay->addWidget(title);
    lay->addLayout(form);
    lay->addWidget(m_info);
    lay->addSpacing(6);
    lay->addWidget(m_bar);
    lay->addWidget(m_status);
    lay->addLayout(buttons);

    const QString base = suggestedName.isEmpty() ? "Sequence 01" : suggestedName;
    m_path->setText(QDir(m_dir).filePath(base + ".mp4"));

    connect(m_browse, &QPushButton::clicked, this, &ExportDialog::browse);
    connect(m_go, &QPushButton::clicked, this, &ExportDialog::startExport);
    connect(m_cancel, &QPushButton::clicked, this, &ExportDialog::reject);
    connect(m_format, &QComboBox::currentIndexChanged, this, &ExportDialog::formatChanged);
    connect(m_range, &QComboBox::currentIndexChanged, this, &ExportDialog::formatChanged);
    formatChanged();
}

void ExportDialog::formatChanged() {
    const QString f = m_format->currentData().toString();
    const bool video = f != "mp3" && f != "wav";
    m_quality->setEnabled(f == "h264" || f == "h265" || f == "vp9" || f == "mp3");
    m_res->setEnabled(video);
    m_fps->setEnabled(video);
    // keep the extension in step with the format
    QFileInfo fi(m_path->text());
    m_path->setText(fi.path() + "/" + fi.completeBaseName() + "." + exportExtension(f));
    double a = 0, b = m_tl.duration();
    if (m_range->currentData().toInt() == 1) {
        if (m_tl.markIn >= 0) a = m_tl.markIn;
        if (m_tl.markOut >= 0) b = m_tl.markOut;
    }
    m_info->setText(QString(TR("Duration: %1")).arg(formatTimecode(std::max(0.0, b - a), m_tl.fps)));
}

void ExportDialog::browse() {
    const QString f = m_format->currentData().toString();
    const QString ext = exportExtension(f);
    const QString path = QFileDialog::getSaveFileName(this, TR("Export to"), m_path->text(), QString("*.%1").arg(ext));
    if (!path.isEmpty()) m_path->setText(path.endsWith("." + ext, Qt::CaseInsensitive) ? path : path + "." + ext);
}

void ExportDialog::setBusy(bool busy) {
    for (QWidget* w : {static_cast<QWidget*>(m_path), static_cast<QWidget*>(m_format), static_cast<QWidget*>(m_quality),
                       static_cast<QWidget*>(m_res), static_cast<QWidget*>(m_fps), static_cast<QWidget*>(m_range),
                       static_cast<QWidget*>(m_browse), static_cast<QWidget*>(m_go)})
        w->setEnabled(!busy);
    m_cancel->setText(busy ? TR("Cancel") : TR("Close"));
    if (!busy) formatChanged();
}

void ExportDialog::startExport() {
    if (m_tl.duration() <= 0) {
        QMessageBox::information(this, TR("Export"), TR("The sequence is empty. Add some clips to the timeline first."));
        return;
    }
    ExportSettings s;
    s.outputPath = m_path->text().trimmed();
    if (s.outputPath.isEmpty()) {
        QMessageBox::warning(this, TR("Export"), TR("Choose where to save the file."));
        return;
    }
    if (QFileInfo::exists(s.outputPath) &&
        QMessageBox::question(this, TR("Export"), TR("This file already exists. Replace it?")) != QMessageBox::Yes)
        return;
    s.format = m_format->currentData().toString();
    s.crf = m_quality->currentData().toInt();
    const int targetH = m_res->currentData().toInt();
    if (targetH == 0) {
        s.width = m_tl.width;
        s.height = m_tl.height;
    } else {
        s.height = targetH;
        s.width = int(std::lround(double(targetH) * m_tl.width / m_tl.height));
    }
    s.width &= ~1;
    s.height &= ~1;
    s.fps = m_fps->currentData().toDouble() > 0 ? m_fps->currentData().toDouble() : m_tl.fps;
    s.start = 0;
    s.end = m_tl.duration();
    if (m_range->currentData().toInt() == 1) {
        if (m_tl.markIn >= 0) s.start = m_tl.markIn;
        if (m_tl.markOut >= 0) s.end = m_tl.markOut;
    }
    if (s.end <= s.start) {
        QMessageBox::warning(this, TR("Export"), TR("The In point must be before the Out point."));
        return;
    }
    if (Exporter::findFfmpeg().isEmpty()) {
        QMessageBox::critical(this, TR("Export"),
                              TR("ffmpeg was not found.\n\nPut ffmpeg (ffmpeg.exe on Windows) in the same folder as "
                              "HRPremiere, or install it and add it to your PATH."));
        return;
    }
    QSettings().setValue("export/dir", QFileInfo(s.outputPath).absolutePath());
    m_bar->setValue(0);
    m_status->setText(TR("Starting…"));
    setBusy(true);
    m_exporter = new Exporter(m_tl, s, this);
    connect(m_exporter, &Exporter::progress, this, [this](double f, const QString& msg) {
        m_bar->setValue(int(f * 1000));
        m_status->setText(msg);
    });
    connect(m_exporter, &Exporter::exportFinished, this, [this](bool ok, const QString& msg) {
        const QString out = m_path->text();
        m_exporter->deleteLater();
        m_exporter = nullptr;
        setBusy(false);
        if (ok) {
            m_bar->setValue(1000);
            m_status->setText(QString("<span style='color:#7bd88f'>✔ %1</span> %2").arg(TR("Export complete:"), out.toHtmlEscaped()));
            m_go->setText(TR("Export again"));
            if (QMessageBox::question(this, TR("Export complete"), TR("Your video is ready.\n\nShow it in its folder?")) == QMessageBox::Yes)
                QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(out).absolutePath()));
        } else {
            m_bar->setValue(0);
            m_status->setText(QString("<span style='color:#e0585f'>%1</span>").arg(msg.toHtmlEscaped()));
        }
    });
    m_exporter->start();
}

void ExportDialog::reject() {
    if (m_exporter) {
        m_exporter->cancel();
        m_status->setText(TR("Cancelling…"));
        return;
    }
    QDialog::reject();
}

void ExportDialog::closeEvent(QCloseEvent* e) {
    if (m_exporter) {
        m_exporter->cancel();
        m_exporter->wait(5000);
    }
    QDialog::closeEvent(e);
}

// ===================================================================== Sequence
SequenceDialog::SequenceDialog(const Timeline& tl, QWidget* parent) : QDialog(parent) {
    setWindowTitle(TR("Sequence Settings"));
    setMinimumWidth(380);
    m_preset = new QComboBox;
    struct P { const char* name; int w, h; };
    const P presets[] = {{"Custom", 0, 0}, {"HD 1080p  (1920 × 1080)", 1920, 1080}, {"HD 720p  (1280 × 720)", 1280, 720},
                         {"4K UHD  (3840 × 2160)", 3840, 2160}, {"Vertical 1080 × 1920 (Reels / TikTok)", 1080, 1920},
                         {"Square 1080 × 1080", 1080, 1080}};
    for (const P& p : presets) m_preset->addItem(TR(p.name), QSize(p.w, p.h));
    m_w = new QSpinBox;
    m_w->setRange(16, 8192);
    m_w->setValue(tl.width);
    m_h = new QSpinBox;
    m_h->setRange(16, 8192);
    m_h->setValue(tl.height);
    m_fps = new QComboBox;
    for (double f : {23.976, 24.0, 25.0, 29.97, 30.0, 50.0, 59.94, 60.0})
        m_fps->addItem(QString::number(f, 'f', f == std::floor(f) ? 0 : 3) + TR(" fps"), f);
    int fi = 0;
    for (int i = 0; i < m_fps->count(); ++i)
        if (std::abs(m_fps->itemData(i).toDouble() - tl.fps) < 0.01) fi = i;
    m_fps->setCurrentIndex(fi);

    // preselect the preset that matches the current size
    for (int i = 1; i < m_preset->count(); ++i)
        if (m_preset->itemData(i).toSize() == QSize(tl.width, tl.height)) m_preset->setCurrentIndex(i);
    connect(m_preset, &QComboBox::currentIndexChanged, this, [this] {
        const QSize s = m_preset->currentData().toSize();
        if (s.isValid() && !s.isEmpty()) { m_w->setValue(s.width()); m_h->setValue(s.height()); }
    });

    auto* form = new QFormLayout;
    form->setVerticalSpacing(10);
    form->setHorizontalSpacing(16);
    form->addRow(TR("Preset"), m_preset);
    form->addRow(TR("Frame width"), m_w);
    form->addRow(TR("Frame height"), m_h);
    form->addRow(TR("Frame rate"), m_fps);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setObjectName("primary");
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(20, 18, 20, 18);
    lay->setSpacing(14);
    auto* title = new QLabel(TR("Sequence Settings"));
    title->setStyleSheet("font-size:16px; font-weight:700;");
    lay->addWidget(title);
    lay->addLayout(form);
    lay->addWidget(bb);
}

// ===================================================================== Speed
SpeedDialog::SpeedDialog(double speedPercent, double durationSeconds, double fps, QWidget* parent)
    : QDialog(parent), m_span(durationSeconds * speedPercent / 100.0), m_fps(fps) {
    setWindowTitle(TR("Clip Speed / Duration"));
    setMinimumWidth(380);
    m_speed = new QDoubleSpinBox;
    m_speed->setRange(5, 2000);
    m_speed->setDecimals(1);
    m_speed->setSingleStep(5);
    m_speed->setSuffix(" %");
    m_speed->setValue(speedPercent);
    m_dur = new QLineEdit(formatTimecode(durationSeconds, fps));
    m_dur->setObjectName("speedDuration");
    m_dur->setToolTip(TR("Type a new duration (HH:MM:SS:FF) and the speed follows"));
    m_ripple = new QCheckBox(TR("Ripple edit, shifting trailing clips"));
    m_ripple->setChecked(true);

    auto syncDuration = [this](double pct) {
        QSignalBlocker b(m_dur);
        m_dur->setText(formatTimecode(m_span / (pct / 100.0), m_fps));
    };
    connect(m_speed, &QDoubleSpinBox::valueChanged, this, syncDuration);
    connect(m_dur, &QLineEdit::editingFinished, this, [this] {
        bool ok = false;
        const double d = parseTimecode(m_dur->text(), m_fps, &ok);
        if (ok && d > 0.05) m_speed->setValue(std::max(5.0, std::min(2000.0, m_span / d * 100.0)));
    });

    auto* form = new QFormLayout;
    form->setVerticalSpacing(10);
    form->setHorizontalSpacing(16);
    form->addRow(TR("Speed"), m_speed);
    form->addRow(TR("Duration"), m_dur);
    auto* hint = new QLabel(TR("100 % is normal speed. Above 100 % is fast motion, below is slow motion. "
                            "The sound changes with the picture."));
    hint->setWordWrap(true);
    hint->setObjectName("dim");
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setObjectName("primary");
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(20, 18, 20, 18);
    lay->setSpacing(12);
    auto* title = new QLabel(TR("Clip Speed / Duration"));
    title->setStyleSheet("font-size:16px; font-weight:700;");
    lay->addWidget(title);
    lay->addLayout(form);
    lay->addWidget(m_ripple);
    lay->addWidget(hint);
    lay->addWidget(bb);
}

// ===================================================================== About
AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(TR("About HR Premiere"));
    setFixedSize(420, 330);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(24, 24, 24, 20);
    lay->setSpacing(8);
    auto* logo = new QLabel;
    logo->setPixmap(icons::logo(96));
    logo->setAlignment(Qt::AlignCenter);
    auto* name = new QLabel(TR("HR Premiere"));
    name->setAlignment(Qt::AlignCenter);
    name->setStyleSheet("font-size:24px; font-weight:700;");
    auto* ver = new QLabel(QString(TR("Version %1")).arg(QCoreApplication::applicationVersion()));
    ver->setAlignment(Qt::AlignCenter);
    ver->setObjectName("dim");
    auto* desc = new QLabel(TR("A video editor built with C++, Qt 6 and FFmpeg.\n"
                            "Multi-track timeline, effects, titles, audio mixing and export."));
    desc->setAlignment(Qt::AlignCenter);
    desc->setWordWrap(true);
    desc->setObjectName("dim");
    auto* ok = new QPushButton("OK");
    ok->setObjectName("primary");
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    auto* owner = new QPushButton("الملكية  ·  Ownership");
    connect(owner, &QPushButton::clicked, this, [this] { OwnershipDialog(this).exec(); });
    lay->addWidget(logo);
    lay->addWidget(name);
    lay->addWidget(ver);
    lay->addSpacing(6);
    lay->addWidget(desc);
    lay->addStretch();
    auto* row = new QHBoxLayout;
    row->addStretch();
    row->addWidget(owner);
    row->addWidget(ok);
    row->addStretch();
    lay->addLayout(row);
}

// ===================================================================== Ownership
static QPixmap pixmapFromResource(const QString& path, QSize box) {
    const qreal dpr = 2.0;
    QPixmap pm(path);
    pm = pm.scaled(QSize(int(box.width() * dpr), int(box.height() * dpr)), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    pm.setDevicePixelRatio(dpr);
    return pm;
}

OwnershipDialog::OwnershipDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("الملكية · Ownership");
    setLayoutDirection(Qt::RightToLeft);   // Arabic page: the first card is on the right
    setMinimumSize(780, 660);

    // animated background: red particles drifting over the dark page
    connect(&m_timer, &QTimer::timeout, this, [this] {
        m_field.update(double(m_clock.restart()));
        update();
    });
    m_timer.setInterval(16);

    auto* title = new QLabel("الملكية");
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("font-size:30px; font-weight:800; background:transparent;");
    auto* subtitle = new QLabel("من يملك البرنامج  ·  Who owns this program");
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setStyleSheet(QString("color:%1; background:transparent;").arg(theme::textDim.name()));

    // One card = a logo (with its name) and, optionally, a line of text.  Semi-transparent so the particles
    // keep flowing behind it.
    auto card = [](const QString& border, QWidget* logo, const QString& roleAr, const QString& roleEn, const QString& roleColor) {
        auto* f = new QFrame;
        f->setObjectName("ownerCard");
        f->setAttribute(Qt::WA_StyledBackground, true);
        f->setStyleSheet(QString("QFrame#ownerCard { background: rgba(18,18,22,160); border:2px solid %1; border-radius:18px; }"
                                 "QFrame#ownerCard QLabel { background:transparent; }").arg(border));
        f->setMinimumSize(330, 420);
        auto* v = new QVBoxLayout(f);
        v->setContentsMargins(24, 26, 24, 24);
        v->setSpacing(10);
        v->addStretch();
        v->addWidget(logo, 0, Qt::AlignCenter);
        v->addStretch();
        if (!roleAr.isEmpty()) {
            auto* line = new QFrame;
            line->setFrameShape(QFrame::HLine);
            line->setStyleSheet(QString("color:%1; background:transparent;").arg(theme::line.name()));
            v->addWidget(line);
            auto* ar = new QLabel(roleAr);
            ar->setAlignment(Qt::AlignCenter);
            ar->setStyleSheet(QString("font-size:22px; font-weight:800; color:%1;").arg(roleColor));
            auto* en = new QLabel(roleEn);
            en->setAlignment(Qt::AlignCenter);
            en->setStyleSheet(QString("color:%1;").arg(theme::textDim.name()));
            v->addWidget(ar);
            v->addWidget(en);
        }
        return f;
    };

    // --- Kaylar: only the logo and the name (no text)
    auto* kaylarLogo = new QWidget;
    auto* kl = new QVBoxLayout(kaylarLogo);
    kl->setContentsMargins(0, 0, 0, 0);
    kl->setSpacing(14);
    auto* character = new QLabel;
    character->setPixmap(pixmapFromResource(":/kaylar_logo.png", QSize(230, 290)));
    character->setAlignment(Qt::AlignCenter);
    auto* word = new QLabel;
    word->setPixmap(pixmapFromResource(":/kaylar_wordmark.png", QSize(240, 48)));
    word->setAlignment(Qt::AlignCenter);
    kl->addWidget(character);
    kl->addWidget(word);

    // --- HR: the logo and the name; HR is the owner and the main developer
    auto* hrLogo = new QWidget;
    auto* hl = new QVBoxLayout(hrLogo);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(12);
    auto* hrImg = new QLabel;
    hrImg->setPixmap(icons::logo(190));
    hrImg->setAlignment(Qt::AlignCenter);
    auto* hrName = new QLabel("HR");
    hrName->setAlignment(Qt::AlignCenter);
    hrName->setStyleSheet("font-size:30px; font-weight:800; letter-spacing:2px;");
    hl->addWidget(hrImg);
    hl->addWidget(hrName);

    auto* cards = new QHBoxLayout;
    cards->setSpacing(26);
    cards->addWidget(card("#ff3b3b", kaylarLogo, QString(), QString(), QString()));
    cards->addWidget(card("#9184d9", hrLogo, "المالك والمطور الأساسي", "Owner & main developer", "#b4a9f2"));

    auto* ok = new QPushButton("OK");
    ok->setObjectName("primary");
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    auto* foot = new QLabel(QString(TR("HR Premiere  ·  v%1")).arg(QCoreApplication::applicationVersion()));
    foot->setAlignment(Qt::AlignCenter);
    foot->setStyleSheet(QString("color:%1; background:transparent;").arg(theme::textDim.name()));

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(30, 26, 30, 22);
    lay->setSpacing(10);
    lay->addWidget(title);
    lay->addWidget(subtitle);
    lay->addSpacing(8);
    lay->addLayout(cards, 1);
    lay->addSpacing(6);
    lay->addWidget(foot);
    lay->addWidget(ok, 0, Qt::AlignCenter);
    m_field.resize(QSizeF(size()));
}

void OwnershipDialog::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor("#0b0b0e"));
    m_field.paint(p);
}

void OwnershipDialog::resizeEvent(QResizeEvent* e) {
    QDialog::resizeEvent(e);
    m_field.resize(QSizeF(size()));
}

void OwnershipDialog::showEvent(QShowEvent* e) {
    QDialog::showEvent(e);
    m_field.resize(QSizeF(size()));
    m_clock.start();
    m_timer.start();
}

void OwnershipDialog::hideEvent(QHideEvent* e) {
    m_timer.stop();
    QDialog::hideEvent(e);
}

// ===================================================================== Shortcuts
ShortcutsDialog::ShortcutsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(TR("Keyboard Shortcuts"));
    resize(520, 560);
    const QVector<QPair<QString, QString>> rows = {
        {"Space", TR("Play / stop")}, {"Left / Right", TR("Step one frame")}, {"Shift + Left / Right", TR("Step five frames")},
        {"Up / Down", TR("Previous / next edit point")}, {"Home / End", TR("Go to start / end")},
        {"V", TR("Selection tool")}, {"A", TR("Track Select Forward tool")}, {"B", TR("Ripple Edit tool")}, {"C", TR("Razor tool")},
        {"H", TR("Hand tool")}, {"Z", TR("Zoom tool")}, {"T", TR("Type tool (adds a title)")},
        {"Ctrl + K", TR("Add edit (split) at the playhead")}, {"Ctrl + Shift + K", TR("Add edit to all tracks")},
        {"Delete", TR("Delete selected clips")}, {"Shift + Delete", TR("Ripple delete")},
        {"Ctrl + Z / Ctrl + Shift + Z", TR("Undo / redo")}, {"Ctrl + C / X / V", TR("Copy / cut / paste clips")},
        {"Ctrl + A", TR("Select all clips")}, {"Ctrl + L", TR("Link / unlink selected clips")},
        {"Ctrl + R", TR("Clip speed / duration (slow or fast motion)")}, {"Ctrl + Shift + R", TR("Record a voice-over (microphone)")},
        {"I / O", TR("Mark In / Mark Out")}, {"Ctrl + Shift + X", TR("Clear In and Out")}, {"M", TR("Add marker")},
        {"S", TR("Toggle snapping")}, {"+ / -", TR("Zoom timeline in / out")}, {"\\", TR("Zoom timeline to fit")},
        {",  /  .", TR("Insert / overwrite from Source monitor")},
        {"Ctrl + I", TR("Import media")}, {"Ctrl + S", TR("Save project")}, {"Ctrl + M", TR("Export media")},
    };
    auto* t = new QTableWidget(rows.size(), 2);
    t->setHorizontalHeaderLabels({TR("Shortcut"), TR("Action")});
    t->verticalHeader()->setVisible(false);
    t->horizontalHeader()->setStretchLastSection(true);
    t->setColumnWidth(0, 210);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionMode(QAbstractItemView::NoSelection);
    t->setShowGrid(false);
    for (int i = 0; i < rows.size(); ++i) {
        t->setItem(i, 0, new QTableWidgetItem(rows[i].first));
        t->setItem(i, 1, new QTableWidgetItem(rows[i].second));
    }
    auto* lay = new QVBoxLayout(this);
    lay->addWidget(t);
}

// ===================================================================== Language (first start)
LanguageDialog::LanguageDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QString::fromUtf8("HR Premiere  ·  اللغة / Language"));
    setMinimumWidth(460);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(28, 24, 28, 24);
    lay->setSpacing(14);
    auto* logo = new QLabel;
    logo->setPixmap(icons::logo(72));
    logo->setAlignment(Qt::AlignCenter);
    lay->addWidget(logo);
    auto* title = new QLabel(QString::fromUtf8("اختر لغة البرنامج\nChoose the language of the program"));
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("font-size:17px; font-weight:700;");
    lay->addWidget(title);
    auto* note = new QLabel(QString::fromUtf8("يمكنك تغييرها لاحقاً من تحرير ▸ الإعدادات\nYou can change it later in Edit ▸ Preferences"));
    note->setAlignment(Qt::AlignCenter);
    note->setObjectName("dim");
    lay->addWidget(note);
    auto* row = new QHBoxLayout;
    row->setSpacing(14);
    for (const char* c : {"ar", "en"}) {
        const bool ar = QString(c) == "ar";
        auto* b = new QPushButton(ar ? QString::fromUtf8("العربية") : QString("English"));
        b->setMinimumHeight(64);
        b->setObjectName(ar ? "primary" : "");
        QFont f = b->font();
        f.setPixelSize(20);
        f.setBold(true);
        b->setFont(f);
        connect(b, &QPushButton::clicked, this, [this, c] { m_code = c; accept(); });
        row->addWidget(b);
    }
    lay->addLayout(row);
}

// ===================================================================== Preferences
static QPixmap themeSwatch(const theme::Theme& t) {
    QPixmap pm(112, 70);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(t.line, 1));
    p.setBrush(t.bg1);
    p.drawRoundedRect(QRectF(0.5, 0.5, 111, 69), 7, 7);
    p.setPen(Qt::NoPen);
    p.setBrush(t.bg2);
    p.drawRoundedRect(QRectF(6, 6, 100, 12), 3, 3);                  // menu bar
    p.setBrush(t.bg3);
    p.drawRoundedRect(QRectF(6, 22, 46, 26), 3, 3);                  // monitor
    p.drawRoundedRect(QRectF(56, 22, 50, 26), 3, 3);
    p.setBrush(t.accent);
    p.drawRoundedRect(QRectF(10, 54, 40, 8), 3, 3);                  // clips
    p.setBrush(t.accentHi);
    p.drawRoundedRect(QRectF(54, 54, 24, 8), 3, 3);
    p.setBrush(t.textDim);
    p.drawRoundedRect(QRectF(82, 54, 22, 8), 3, 3);
    return pm;
}

PreferencesDialog::PreferencesDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(TR("Preferences"));
    setMinimumWidth(660);                 // wide enough for four colour samples in a row: all eight fit without scrolling
    m_startTheme = theme::currentId();
    m_startAccent = theme::accentOverride();
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(20, 18, 20, 18);
    lay->setSpacing(12);

    auto* title = new QLabel(TR("Preferences"));
    title->setStyleSheet("font-size:16px; font-weight:700;");
    lay->addWidget(title);

    // ---- language
    auto* langTitle = new QLabel(TR("Language"));
    langTitle->setObjectName("accentText");
    lay->addWidget(langTitle);
    m_lang = new QComboBox;
    m_lang->addItem(i18n::nativeName(i18n::Lang::Arabic), "ar");
    m_lang->addItem(i18n::nativeName(i18n::Lang::English), "en");
    m_lang->setCurrentIndex(std::max(0, m_lang->findData(i18n::code(i18n::current()))));
    lay->addWidget(m_lang);
    auto* langNote = new QLabel(TR("The language changes after HR Premiere is restarted."));
    langNote->setObjectName("dim");
    langNote->setWordWrap(true);
    lay->addWidget(langNote);

    // ---- colours
    auto* themeTitle = new QLabel(TR("Colours"));
    themeTitle->setObjectName("accentText");
    lay->addWidget(themeTitle);
    m_themes = new QListWidget;
    m_themes->setViewMode(QListView::IconMode);
    m_themes->setIconSize(QSize(112, 70));
    m_themes->setMovement(QListView::Static);
    m_themes->setResizeMode(QListView::Adjust);
    m_themes->setWrapping(true);
    m_themes->setSpacing(8);
    m_themes->setUniformItemSizes(true);
    m_themes->setMinimumHeight(250);
    m_themes->setSelectionMode(QAbstractItemView::SingleSelection);
    for (const theme::Theme& t : theme::all()) {
        auto* it = new QListWidgetItem(QIcon(themeSwatch(t)), TR(t.name));
        it->setData(Qt::UserRole, t.id);
        it->setTextAlignment(Qt::AlignHCenter);
        m_themes->addItem(it);
        if (t.id == theme::currentId()) m_themes->setCurrentItem(it);
    }
    lay->addWidget(m_themes);
    connect(m_themes, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* it) {
        if (it) pickTheme(it->data(Qt::UserRole).toString());
    });

    auto* row = new QHBoxLayout;
    m_accentBtn = new QPushButton(TR("Accent colour…"));
    auto* resetAccent = new QPushButton(TR("Theme's own accent"));
    auto* classic = new QPushButton(TR("Back to the classic look"));
    classic->setToolTip(TR("The traditional dark grey look"));
    row->addWidget(m_accentBtn);
    row->addWidget(resetAccent);
    row->addStretch(1);
    row->addWidget(classic);
    lay->addLayout(row);
    connect(m_accentBtn, &QPushButton::clicked, this, &PreferencesDialog::chooseAccent);
    connect(resetAccent, &QPushButton::clicked, this, [this] {
        theme::use(theme::currentId());
        theme::refresh();
    });
    connect(classic, &QPushButton::clicked, this, [this] {
        theme::use(theme::kClassicId);
        theme::refresh();
        for (int i = 0; i < m_themes->count(); ++i)
            if (m_themes->item(i)->data(Qt::UserRole).toString() == theme::kClassicId) {
                QSignalBlocker b(m_themes);
                m_themes->setCurrentRow(i);
            }
    });

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setObjectName("primary");
    bb->button(QDialogButtonBox::Ok)->setText(TR("OK"));
    bb->button(QDialogButtonBox::Cancel)->setText(TR("Cancel"));
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
}

void PreferencesDialog::pickTheme(const QString& id) {
    theme::use(id, theme::accentOverride());
    theme::refresh();
}

void PreferencesDialog::chooseAccent() {
    const QColor c = QColorDialog::getColor(theme::accent, this, TR("Accent colour"));
    if (!c.isValid()) return;
    theme::use(theme::currentId(), c);
    theme::refresh();
}

bool PreferencesDialog::languageChanged() const { return chosenLanguageCode() != i18n::code(i18n::current()); }
QString PreferencesDialog::chosenLanguageCode() const { return m_lang->currentData().toString(); }

void PreferencesDialog::accept() {
    theme::save();
    if (languageChanged()) i18n::saveLanguageCode(chosenLanguageCode());
    QDialog::accept();
}

void PreferencesDialog::reject() {
    theme::use(m_startTheme, m_startAccent);
    theme::refresh();
    QDialog::reject();
}

}  // namespace hr
