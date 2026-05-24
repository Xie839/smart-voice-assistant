#ifndef TRANSCRIPTASSEMBLER_H
#define TRANSCRIPTASSEMBLER_H

#include <QString>

class TranscriptAssembler
{
public:
    TranscriptAssembler();

    QString appendSegment(const QString &text, qint64 gapMs);
    void clear();
    QString currentText() const;

private:
    QString m_text;
};

#endif
