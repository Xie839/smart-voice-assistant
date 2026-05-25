#include "PerfTracer.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QProcessEnvironment>
#include <QTextStream>

#include <utility>

namespace
{
struct TraceState
{
    qint64 startMs = 0;
    qint64 lastMs = 0;
};

QElapsedTimer &globalTimer()
{
    static QElapsedTimer timer;
    static bool started = false;
    if (!started)
    {
        timer.start();
        started = true;
    }
    return timer;
}

QMutex &traceMutex()
{
    static QMutex mutex;
    return mutex;
}

QHash<QString, TraceState> &traceStates()
{
    static QHash<QString, TraceState> states;
    return states;
}

QMutex &logFileMutex()
{
    static QMutex mutex;
    return mutex;
}

QString traceKey(const QString &category, const QString &traceId)
{
    return category + "|" + traceId;
}

QString timestamp()
{
    return QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
}

void appendPerfLineToFile(const QString &line)
{
    if (!PerfTracer::isEnabled())
    {
        return;
    }

    QMutexLocker locker(&logFileMutex());
    const QString path = PerfTracer::logFilePath();
    if (path.isEmpty())
    {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    {
        qWarning().noquote() << QString("[PERF][WARN] cannot open perf log file: %1").arg(path);
        return;
    }

    QTextStream out(&file);
    out << timestamp() << " " << line << '\n';
}

void printPerfLine(const QString &category, const QString &traceId, const QString &message)
{
    if (!PerfTracer::isEnabled())
    {
        return;
    }
    const QString line = QString("[PERF][%1][trace=%2] %3").arg(category, traceId, message);
    qDebug().noquote() << line;
    appendPerfLineToFile(line);
}
}

PerfTracer::PerfTracer(QString traceId, QString taskName)
    : m_traceId(std::move(traceId)),
      m_taskName(std::move(taskName))
{
    m_totalTimer.start();
    m_stageTimer.start();
    m_lastStage = "start";
    startTrace(m_taskName, m_traceId, "start");
}

void PerfTracer::mark(const QString &stageName)
{
    const qint64 stageMs = m_stageTimer.elapsed();
    const qint64 totalMs = m_totalTimer.elapsed();
    if (isEnabled())
    {
        const QString line = QString("[PERF][%1][trace=%2] %3: +%4 ms, total=%5 ms")
                                 .arg(m_taskName, m_traceId, stageName)
                                 .arg(stageMs)
                                 .arg(totalMs);
        qDebug().noquote() << line;
        appendPerfLineToFile(line);
    }
    m_lastStage = stageName;
    m_stageTimer.restart();
}

void PerfTracer::markDuration(const QString &stageName, qint64 durationMs)
{
    markDurationTrace(m_taskName, m_traceId, stageName, durationMs);
}

void PerfTracer::finish(const QString &finalStageName)
{
    mark(finalStageName);
}

QString PerfTracer::traceId() const
{
    return m_traceId;
}

bool PerfTracer::isEnabled()
{
#ifdef QT_DEBUG
    return true;
#else
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString value = env.value("VOICEFLOW_PERF_LOG").trimmed().toLower();
    return value == "1" || value == "true" || value == "yes" || value == "on";
#endif
}

qint64 PerfTracer::nowMs()
{
    return globalTimer().elapsed();
}

void PerfTracer::startTrace(const QString &category,
                            const QString &traceId,
                            const QString &stageName,
                            const QString &details)
{
    if (traceId.trimmed().isEmpty())
    {
        return;
    }

    const qint64 now = nowMs();
    {
        QMutexLocker locker(&traceMutex());
        traceStates().insert(traceKey(category, traceId), TraceState{now, now});
    }

    QString message = "start: " + stageName;
    if (!details.trimmed().isEmpty())
    {
        message += ", " + details;
    }
    printPerfLine(category, traceId, message);
}

void PerfTracer::markTrace(const QString &category,
                           const QString &traceId,
                           const QString &stageName,
                           const QString &details)
{
    if (traceId.trimmed().isEmpty())
    {
        return;
    }

    const qint64 now = nowMs();
    qint64 delta = 0;
    qint64 total = 0;
    {
        QMutexLocker locker(&traceMutex());
        auto &states = traceStates();
        const QString key = traceKey(category, traceId);
        if (!states.contains(key))
        {
            states.insert(key, TraceState{now, now});
        }
        TraceState state = states.value(key);
        delta = now - state.lastMs;
        total = now - state.startMs;
        state.lastMs = now;
        states.insert(key, state);
    }

    QString message = QString("%1: +%2 ms, total=%3 ms").arg(stageName).arg(delta).arg(total);
    if (!details.trimmed().isEmpty())
    {
        message += ", " + details;
    }
    printPerfLine(category, traceId, message);
}

void PerfTracer::markDurationTrace(const QString &category,
                                   const QString &traceId,
                                   const QString &stageName,
                                   qint64 durationMs,
                                   const QString &details)
{
    if (traceId.trimmed().isEmpty())
    {
        return;
    }

    QString message = QString("%1: %2 ms").arg(stageName).arg(durationMs);
    if (!details.trimmed().isEmpty())
    {
        message += ", " + details;
    }
    printPerfLine(category, traceId, message);
}

void PerfTracer::warnIfSlow(const QString &category,
                            const QString &traceId,
                            const QString &stageName,
                            qint64 durationMs,
                            qint64 thresholdMs,
                            const QString &hint)
{
    if (!isEnabled() || durationMs <= thresholdMs)
    {
        return;
    }
    const QString line = QString("[PERF][WARN][%1][trace=%2] %3=%4 ms, %5")
                             .arg(category, traceId, stageName)
                             .arg(durationMs)
                             .arg(hint);
    qWarning().noquote() << line;
    appendPerfLineToFile(line);
}

QString PerfTracer::logFilePath()
{
    QDir dir(QCoreApplication::applicationDirPath());
    if (!dir.exists("logs") && !dir.mkpath("logs"))
    {
        return QString();
    }
    return dir.filePath("logs/perf-" + QDateTime::currentDateTime().toString("yyyyMMdd") + ".log");
}

QString PerfTracer::shortFileName(const QString &path)
{
    return QFileInfo(path).fileName();
}
