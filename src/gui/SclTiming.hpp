#pragma once

#include <QDebug>
#include <QElapsedTimer>

namespace iec61850sim
{
// Enabled only for explicit local performance runs. Never logs engineering fields.
class SclTiming
{
public:
    explicit SclTiming(const char* label) : m_label(label)
    {
        static const bool enabled = qEnvironmentVariableIntValue("IEC61850_SCL_TIMING") == 1;
        if (enabled)
            m_timer.start();
    }

    ~SclTiming()
    {
        if (m_timer.isValid())
            qInfo().nospace() << "SCL timing " << m_label << " ms=" << m_timer.nsecsElapsed() / 1e6;
    }

private:
    const char* m_label;
    QElapsedTimer m_timer;
};
}
