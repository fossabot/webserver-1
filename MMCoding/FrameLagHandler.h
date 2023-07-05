#ifndef FRAME_LAG_HANDLER_H
#define FRAME_LAG_HANDLER_H

#include <deque>

namespace NMMSS
{

class FrameLagHandler
{
public:
    FrameLagHandler()
    {
        Clear();
    }

    void RegisterInputFrame(::uint64_t timestamp, bool preroll, bool keySample = false)
    {
        m_data.push_back({ timestamp, preroll, keySample });
    }

    bool RegisterOutputFrame()
    {
        if (m_data.empty())
        {
            return false;
        }

        m_lastData = m_data.front();
        m_data.pop_front();
        return !m_lastData.Preroll;
    }

    ::uint64_t LastTimestamp()
    {
        return m_lastData.Timestamp;
    }

    bool LastKeySample()
    {
        return m_lastData.KeySample;
    }

    void Clear()
    {
        m_data.clear();
        m_lastData = {};
        m_checked = false;
    }

    void SkipAll()
    {
        for (auto it = m_data.begin(); it != m_data.end(); ++it)
        {
            it->Preroll = true;
        }
    }

    bool Check()
    {
        if (m_checked || m_data.size() < 10ull)
            return true;

        m_checked = true;
        return false;
    }

    int Count()
    {
        return (int)m_data.size();
    }

private:
    class FrameData
    {
    public:
        ::uint64_t Timestamp;
        bool Preroll;
        bool KeySample;
    };

    std::deque<FrameData> m_data;
    FrameData m_lastData;
    bool m_checked;
};

};

#endif
