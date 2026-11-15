#include "ui/Icons.h"
#include "ui/AiPanel.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"
#include "core/EditOps.h"
#include "core/I18n.h"
#include "ai/TranscribeCli.h"
#include "ui/Dialogs.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QSplashScreen>
#include <QTimer>

using namespace hr;

static QPixmap makeSplash() {
    const int W = 560, H = 320;
    QPixmap pm(W * 2, H * 2);
    pm.setDevicePixelRatio(2);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(theme::bg1);
    p.setPen(QPen(theme::accentDim, 2));
    p.drawRoundedRect(QRectF(2, 2, W - 4, H - 4), 18, 18);
    p.drawPixmap(QRect(W / 2 - 64, 52, 128, 128), icons::logo(128));
    QFont f = p.font();
    f.setPixelSize(30);
    f.setBold(true);
    p.setFont(f);
    p.setPen(theme::text);
    p.drawText(QRect(0, 196, W, 40), Qt::AlignCenter, "HR Premiere");
    f.setPixelSize(13);
    f.setBold(false);
    p.setFont(f);
    p.setPen(theme::textDim);
    p.drawText(QRect(0, 238, W, 24), Qt::AlignCenter, TR("Loading…"));
    return pm;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("HRPremiere");
    app.setOrganizationName("HR");
    app.setApplicationVersion("0.1.0");
    app.setWindowIcon(QIcon(icons::logo(64)));

    QCommandLineParser parser;
    parser.setApplicationDescription("HR Premiere – video editor");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("files", "A project (.hrproj) or media files to import.", "[files...]");
    QCommandLineOption place("place", "Lay the given media out one after another on the timeline.");
    QCommandLineOption demo("demo", "Fill the timeline with a titled, graded demo sequence (for screenshots).");
    QCommandLineOption shot("screenshot", "Save a screenshot of the window to <file> and quit.", "file");
    QCommandLineOption delay("delay", "Milliseconds to wait before the screenshot (default 1500).", "ms", "1500");
    QCommandLineOption size("size", "Window size, e.g. 1500x900.", "WxH");
    QCommandLineOption ws("workspace", "Workspace to start in.", "name");
    QCommandLineOption playFor("play", "Start playback right away (for testing).");
    QCommandLineOption dialog("dialog", "With --screenshot: capture a dialog (export, sequence, about, ownership, shortcuts, preferences, language) instead of the window.", "name");
    QCommandLineOption transcribe("transcribe", "Headless speech recognition of <file> (downloads the model if needed).", "file");
    QCommandLineOption model("model", "Speech model for --transcribe: tiny, base, small (default) or turbo.", "id", "small");
    QCommandLineOption language("language", "Spoken language for --transcribe, e.g. ar, en (default: auto).", "code", "auto");
    QCommandLineOption outFile("out", "Where --transcribe writes the subtitles (.srt).", "file");
    QCommandLineOption langOpt("lang", "Interface language for this run: ar (Arabic) or en (English).", "code");
    QCommandLineOption themeOpt("theme", "Colour theme for this run: hr, classic, midnight, ocean, emerald, sunset, rose or light.", "id");
    QCommandLineOption cleanOpt("clean", "Audio clean-up before --transcribe: none, light, strong or auto (default).", "level", "auto");
    QCommandLineOption prefilterOpt("prefilter", "An ffmpeg audio filter chain to run before --transcribe (for experiments).", "filter");
    QCommandLineOption chatOpt("chat", "Send a request to the AI assistant's chat after the start (can be repeated; for screenshots and scripts).", "text");
    parser.addOptions({place, demo, shot, delay, size, ws, playFor, dialog, transcribe, model, language, outFile, langOpt, themeOpt, cleanOpt, prefilterOpt, chatOpt});
    parser.process(app);

    // colours: what the user chose in Preferences, or --theme for this run only
    if (parser.isSet(themeOpt)) theme::use(parser.value(themeOpt));
    else theme::loadSaved();
    theme::apply(app);

    if (parser.isSet(transcribe))
        return hr::ai::runTranscribeCli(parser.value(transcribe), parser.value(model), parser.value(language), parser.value(outFile),
                                        parser.value(cleanOpt), parser.value(prefilterOpt));

    const bool screenshotMode = parser.isSet(shot);

    // language: --lang, then HR_LANG, then what the installer / the first start / Preferences stored.
    // The very first start asks (Arabic or English); automation modes never ask and use English.
    QString langCode = parser.value(langOpt);
    if (langCode.isEmpty()) langCode = qEnvironmentVariable("HR_LANG");
    if (langCode.isEmpty()) langCode = i18n::savedLanguageCode();
    if (langCode.isEmpty() && !screenshotMode) {
        LanguageDialog chooser;
        chooser.exec();
        langCode = chooser.code();
        i18n::saveLanguageCode(langCode);
    }
    i18n::setCurrent(i18n::fromCode(langCode, i18n::Lang::English));
    if (i18n::isRtl()) app.setLayoutDirection(Qt::RightToLeft);

    QSplashScreen splash(makeSplash());
    if (!screenshotMode) {
        splash.show();
        app.processEvents();
    }

    MainWindow window(!screenshotMode);
    if (parser.isSet(size)) {
        const QStringList wh = parser.value(size).split('x');
        if (wh.size() == 2) window.resize(wh[0].toInt(), wh[1].toInt());
    }
    if (parser.isSet(ws)) window.applyWorkspace(parser.value(ws));
    window.show();
    if (!screenshotMode) splash.finish(&window);

    QStringList mediaFiles;
    for (const QString& a : parser.positionalArguments()) {
        if (a.endsWith(".hrproj", Qt::CaseInsensitive)) window.openProjectFile(a);
        else mediaFiles << a;
    }
    if (!mediaFiles.isEmpty()) {
        if (parser.isSet(place) || parser.isSet(demo)) window.autoPlace(mediaFiles);
        else window.importFiles(mediaFiles);
    }
    if (parser.isSet(demo)) {
        Project* p = window.project();
        p->edit("Demo", [](Timeline& tl) {
            tl.video[1].clips.push_back(ops::makeTitle(1.0, 3.5, "HR Premiere"));
            if (!tl.video[0].clips.isEmpty()) {
                Clip& c = tl.video[0].clips[0];
                c.effects.push_back(EffectRegistry::make("color_correction"));
                c.effect("color_correction")->set("saturation", 130.0);
                c.effects.push_back(EffectRegistry::make("vignette"));
                c.effect("opacity")->set("fade_in", 0.5);
            }
        });
        if (!p->timeline().video[0].clips.isEmpty()) p->setSelection({p->timeline().video[0].clips[0].id});
        window.timeline()->zoomToFit();
        window.program()->seek(1.6);
    }
    {
        int at = 200;                                                  // the requests of --chat, one after the other
        for (const QString& line : parser.values(chatOpt)) {
            QTimer::singleShot(at, &window, [&window, line] { window.assistant()->sendChat(line); });
            at += 200;
        }
    }
    if (parser.isSet(playFor)) QTimer::singleShot(300, window.program(), &Player::play);

    if (screenshotMode) {
        const QString out = parser.value(shot);
        QDialog* dlg = nullptr;
        if (parser.isSet(dialog)) {
            const QString d = parser.value(dialog);
            if (d == "export") dlg = new ExportDialog(window.project()->timeline(), "My Video", &window);
            else if (d == "sequence") dlg = new SequenceDialog(window.project()->timeline(), &window);
            else if (d == "about") dlg = new AboutDialog(&window);
            else if (d == "ownership") dlg = new OwnershipDialog(&window);
            else if (d == "shortcuts") dlg = new ShortcutsDialog(&window);
            else if (d == "preferences") dlg = new PreferencesDialog(&window);
            else if (d == "language") dlg = new LanguageDialog(&window);
            if (dlg) dlg->show();
        }
        QTimer::singleShot(parser.value(delay).toInt(), &app, [&window, &app, out, dlg] {
            const bool ok = (dlg ? dlg->grab() : window.grab()).save(out);
            if (!ok) qWarning("could not write %s", qPrintable(out));
            app.exit(ok ? 0 : 1);
        });
    }
    return app.exec();
}
