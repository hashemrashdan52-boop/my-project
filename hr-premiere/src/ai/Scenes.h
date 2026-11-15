#pragma once
#include <QString>
#include <QVector>
#include <atomic>
#include <functional>

namespace hr::ai {

struct SceneOptions {
    double sensitivity = 0.5;      // 0..1, higher finds more (smaller) changes
    double minSceneSeconds = 0.8;  // never report two cuts closer than this
};

// Times (seconds from the start of the file) at which the picture changes to a different scene.
// Runs on the calling thread; `progress` gets 0..1; set `cancel` to stop early.
QVector<double> detectScenes(const QString& path, const SceneOptions& opt,
                             const std::function<void(double)>& progress = {}, const std::atomic<bool>* cancel = nullptr);

}  // namespace hr::ai
