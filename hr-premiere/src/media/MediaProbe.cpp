#include "core/I18n.h"
#include "MediaProbe.h"

#include <QFileInfo>
#include <QImageReader>
#include <cmath>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
}

namespace hr {

static const QStringList kImageExt = {"png", "jpg", "jpeg", "bmp", "webp", "tif", "tiff", "jfif", "ico"};

bool isStillImagePath(const QString& path) {
    return kImageExt.contains(QFileInfo(path).suffix().toLower());
}

bool probeMedia(const QString& path, MediaItem& item, QString* error) {
    if (!QFileInfo::exists(path)) {
        if (error) *error = TR("File not found");
        return false;
    }
    item.path = path;
    item.markIn = item.markOut = -1;
    item.offline = false;

    if (isStillImagePath(path)) {
        QImageReader rd(path);
        const QSize sz = rd.size();
        if (sz.isValid()) {
            item.kind = MediaKind::Image;
            item.width = sz.width();
            item.height = sz.height();
            item.fps = 0;
            item.duration = 5.0;
            item.hasVideo = true;
            item.hasAudio = false;
            return true;
        }
    }

    AVFormatContext* fmt = nullptr;
    int r = avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr);
    if (r < 0) {
        if (error) *error = TR("Unsupported or corrupt file");
        return false;
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        if (error) *error = TR("Could not read the file's streams");
        return false;
    }
    int vIdx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    int aIdx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (vIdx >= 0 && (fmt->streams[vIdx]->disposition & AV_DISPOSITION_ATTACHED_PIC)) vIdx = -1;  // cover art

    item.hasVideo = vIdx >= 0;
    item.hasAudio = aIdx >= 0;
    if (!item.hasVideo && !item.hasAudio) {
        avformat_close_input(&fmt);
        if (error) *error = TR("No audio or video found in this file");
        return false;
    }
    if (fmt->duration > 0) item.duration = double(fmt->duration) / AV_TIME_BASE;
    else item.duration = 0;

    if (item.hasVideo) {
        AVStream* st = fmt->streams[vIdx];
        item.width = st->codecpar->width;
        item.height = st->codecpar->height;
        // phone videos store their orientation as metadata: report the size as it is displayed
        if (const AVPacketSideData* sd = av_packet_side_data_get(st->codecpar->coded_side_data, st->codecpar->nb_coded_side_data,
                                                                 AV_PKT_DATA_DISPLAYMATRIX)) {
            if (sd->size >= 9 * int(sizeof(int32_t))) {
                const double theta = av_display_rotation_get(reinterpret_cast<const int32_t*>(sd->data));
                const int rot = ((int(std::lround(-theta)) % 360) + 360) % 360;
                if (rot == 90 || rot == 270) std::swap(item.width, item.height);
            }
        }
        const AVRational fr = av_guess_frame_rate(fmt, st, nullptr);
        item.fps = (fr.num > 0 && fr.den > 0) ? av_q2d(fr) : 25.0;
        if (item.duration <= 0 && st->duration > 0) item.duration = st->duration * av_q2d(st->time_base);
        const QString fname = QString::fromUtf8(fmt->iformat ? fmt->iformat->name : "");
        const bool singleFrame = st->nb_frames == 1 || (item.duration <= 0.05);
        if (fname.contains("image2") || fname.endsWith("_pipe") || singleFrame) {
            item.kind = MediaKind::Image;
            item.duration = 5.0;
            item.hasAudio = false;
            item.fps = 0;
        } else {
            item.kind = MediaKind::Video;
        }
    } else {
        item.kind = MediaKind::Audio;
    }
    if (item.hasAudio) {
        AVStream* st = fmt->streams[aIdx];
        item.sampleRate = st->codecpar->sample_rate;
        item.channels = st->codecpar->ch_layout.nb_channels;
        if (item.duration <= 0 && st->duration > 0) item.duration = st->duration * av_q2d(st->time_base);
    }
    avformat_close_input(&fmt);
    if (item.duration <= 0) {
        if (error) *error = TR("Could not determine the duration of this file");
        return false;
    }
    return true;
}

}  // namespace hr
