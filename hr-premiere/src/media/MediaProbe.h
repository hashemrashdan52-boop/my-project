#pragma once
#include "core/Model.h"

namespace hr {
// Fills duration / size / fps / streams of `item` (item.path must be set).
bool probeMedia(const QString& path, MediaItem& item, QString* error = nullptr);
bool isStillImagePath(const QString& path);
}  // namespace hr
