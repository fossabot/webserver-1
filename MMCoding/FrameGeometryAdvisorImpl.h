#ifndef FRAME_GEOMETRY_ADVISOR_IMPL_H
#define FRAME_GEOMETRY_ADVISOR_IMPL_H

#include "../MMCoding/FrameGeometryAdvisor.h"
#include <CorbaHelpers/RefcountedImpl.h>

namespace NMMSS
{

class CFrameGeometryAdvisor : public NCorbaHelpers::CRefcountedImpl, public NMMSS::IFrameGeometryAdvisor
{
public:
    CFrameGeometryAdvisor(int width = 0, int height = 0, IFrameGeometryAdvisor::EAdviceType type = IFrameGeometryAdvisor::EAdviceType::ATDontCare)
    {
        Setup(width, height, type);
    }
public:
    IFrameGeometryAdvisor::EAdviceType GetAdvice(uint32_t& width, uint32_t& height) const override
    {
        width = m_width;
        height = m_height;
        return m_type;
    }
    void Setup(int width, int height, IFrameGeometryAdvisor::EAdviceType type)
    {
        m_width = width;
        m_height = height;
        m_type = type;
    }
private:
    int m_width;
    int m_height;
    IFrameGeometryAdvisor::EAdviceType m_type;
};

}

#endif
