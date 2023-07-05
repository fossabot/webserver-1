#include "TelemetryHelper.h"
#include <CorbaHelpers/RefcountedImpl.h>

namespace
{
    class CPreset : public NMMSS::IPreset
        , public virtual NCorbaHelpers::CRefcountedImpl
    {
    public:
        CPreset(unsigned long pos, const wchar_t* label, bool savedOnDevice, NMMSS::AbsolutePositionInformation positionInfo)
            : m_pos(pos)
            , m_positionInfo(positionInfo)
            , m_label(label)
            , m_savedOnDevice(savedOnDevice)
        {
        }

        virtual unsigned long Position() const
        {
            return m_pos;
        }

        virtual const wchar_t* Name() const
        {
            return m_label.c_str();
        }

        virtual bool IsSavedOnDevice() const
        {
            return m_savedOnDevice;
        }

        virtual NMMSS::AbsolutePositionInformation PositionInfo() const
        {
            return m_positionInfo;
        }

    private:
        unsigned long m_pos;
        NMMSS::AbsolutePositionInformation m_positionInfo;
        std::basic_string<wchar_t> m_label;
        bool m_savedOnDevice;
    };

    class CFlaggedRange : public NMMSS::IFlaggedRange
        , public virtual NCorbaHelpers::CRefcountedImpl
    {
    public:
        CFlaggedRange(NMMSS::ERangeFlag flag, long min,
            long max)
            : m_flag(flag)
            , m_min(min)
            , m_max(max)
        {
        }

        ~CFlaggedRange()
        {
        }

        virtual NMMSS::ERangeFlag GetRangeFlag() const
        {
            return m_flag;
        }

        virtual long GetMinValue() const
        {
            return m_min;
        }

        virtual long GetMaxValue() const
        {
            return m_max;
        }

    private:
        NMMSS::ERangeFlag m_flag;
        long m_min;
        long m_max;
    };
}

NMMSS::IPreset* CreatePreset(unsigned long pos, const wchar_t* label, bool savedOnDevice, NMMSS::AbsolutePositionInformation positionInfo)
{
    return new CPreset(pos, label, savedOnDevice, positionInfo);
}

NMMSS::IFlaggedRange* CreateFlaggedRange(NMMSS::ERangeFlag flag, long min, long max)
{
    return new CFlaggedRange(flag, min, max);
}
