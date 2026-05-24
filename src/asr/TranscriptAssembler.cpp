#include "TranscriptAssembler.h"

TranscriptAssembler::TranscriptAssembler() = default;

QString TranscriptAssembler::appendSegment(const QString &text, qint64 gapMs)
{
    const QString t = text.trimmed();
    if (t.isEmpty())
    {
        return m_text;
    }

    if (m_text.trimmed().isEmpty())
    {
        m_text = t;
        return m_text;
    }

    if (gapMs >= 1600)
    {
        if (!m_text.endsWith('\n'))
        {
            m_text += '\n';
        }
        m_text += t;
        return m_text;
    }

    if (!m_text.endsWith(' ') && !m_text.endsWith('\n'))
    {
        m_text += ' ';
    }
    m_text += t;
    return m_text;
}

void TranscriptAssembler::clear()
{
    m_text.clear();
}

QString TranscriptAssembler::currentText() const
{
    return m_text;
}
