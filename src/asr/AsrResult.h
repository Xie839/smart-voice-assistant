#ifndef ASRRESULT_H
#define ASRRESULT_H

#include <QMetaType>
#include <QString>

// 一次 ASR 识别任务的结果对象。后台线程完成后通过 Qt signal 返回给主线程，
// MainWindow 只把 text 追加到“原始识别文本”框，不显示模型路径或 chunk 路径。
struct AsrResult
{
    QString wavPath;
    QString text;
    QString modelName;
    qint64 sequenceId = -1;
    qint64 startTimeMs = -1;
    qint64 endTimeMs = -1;
    qint64 elapsedMs = 0;
    bool success = false;
    QString errorMessage;
};

Q_DECLARE_METATYPE(AsrResult)

#endif
