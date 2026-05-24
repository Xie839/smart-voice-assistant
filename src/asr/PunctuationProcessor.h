#ifndef PUNCTUATIONPROCESSOR_H
#define PUNCTUATIONPROCESSOR_H

#include <QObject>
#include <QQueue>
#include <QString>

class QProcess;
class QTimer;

class PunctuationProcessor : public QObject
{
    Q_OBJECT

public:
    explicit PunctuationProcessor(QObject *parent = nullptr);
    ~PunctuationProcessor() override;

    bool isAvailable(QString *reason = nullptr) const;
    void punctuateAsync(const QString &text);

signals:
    void punctuateFinished(const QString &punctuatedText);
    void punctuateError(const QString &errorMessage);

private:
    QString findProjectFile(const QString &relativePath) const;
    QString findPunctuationExe() const;
    QString findPunctuationModel() const;
    QString extractPunctuationText(const QString &stdoutText, QString *errorMessage) const;
    QString fallbackRulePunctuation(const QString &text) const;

    void startNext();
    void finishCurrent(const QString &outputText, const QString &errorMessage = QString());

private:
    static constexpr int kPunctuationTimeoutMs = 20000;

    QQueue<QString> pendingTexts;
    bool running = false;
    QString activeText;
    QProcess *activeProcess = nullptr;
    QTimer *activeTimeoutTimer = nullptr;
    bool activeTimedOut = false;
    bool activeCompleted = false;
};

#endif
