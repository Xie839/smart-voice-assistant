#ifndef TASKCONFIG_H
#define TASKCONFIG_H

#include <QString>

struct TaskConfig
{
    QString modelMode = "base";       // tiny / base / small
    QString modelText = "base 均衡模式";

    QString textMode = "recognize";   // recognize / offline
    QString textModeText = "仅识别";

    QString wordLib = "general";      // general / code / academic / custom
    QString wordLibText = "通用词库";

    bool aiConfigured = false;
    QString aiProvider = "";
};

#endif