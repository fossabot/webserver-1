#pragma once

#include "../Sample.h"

namespace NMMSS
{

class SessionWatcher
{
public:
    void RegisterStartSample(NMMSS::ISample* sample)
    {
        std::uint32_t sessionId = -1;
        m_sessionChanged = !NMMSS::GetSampleSessionId(sample, sessionId) || (sessionId != m_lastSessionId);
        m_lastSessionId = sessionId;
    }

    bool SessionChanged() const
    {
        return m_sessionChanged;
    }

private:
    std::uint32_t m_lastSessionId = -1;
    bool m_sessionChanged = true;
};

};
