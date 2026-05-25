#ifndef PERFTRACER_H
#define PERFTRACER_H

#include <QElapsedTimer>
#include <QString>

class PerfTracer
{
public:
    explicit PerfTracer(QString traceId, QString taskName);

    void mark(const QString &stageName);
    void markDuration(const QString &stageName, qint64 durationMs);
    void finish(const QString &finalStageName = "finished");

    QString traceId() const;

    static bool isEnabled();
    static qint64 nowMs();
    static void startTrace(const QString &category,
                           const QString &traceId,
                           const QString &stageName,
                           const QString &details = QString());
    static void markTrace(const QString &category,
                          const QString &traceId,
                          const QString &stageName,
                          const QString &details = QString());
    static void markDurationTrace(const QString &category,
                                  const QString &traceId,
                                  const QString &stageName,
                                  qint64 durationMs,
                                  const QString &details = QString());
    static void warnIfSlow(const QString &category,
                           const QString &traceId,
                           const QString &stageName,
                           qint64 durationMs,
                           qint64 thresholdMs,
                           const QString &hint);
    static QString logFilePath();
    static QString shortFileName(const QString &path);

private:
    QString m_traceId;
    QString m_taskName;
    QElapsedTimer m_totalTimer;
    QElapsedTimer m_stageTimer;
    QString m_lastStage;
};

#endif
