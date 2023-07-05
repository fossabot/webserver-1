#pragma once

#include "../mIntTypes.h"
#include <CorbaHelpers/Refcounted.h>

namespace NMMSS
{

/// Interface giving "advice" for desired/preferred frame size
/// For example used by decoder to determine resulting frame size
/// Usually implemented be frames consumer, e.g. monitor viewport or detector
class IFrameGeometryAdvisor : public virtual NCorbaHelpers::IRefcounted
{
public:
    typedef enum {
         ATDontCare    =0               // No frame size preferences
    ,    ATLargest     =1               // Maximum possible frame size
    ,    ATSmallest    =2               // Minimum possible frame size
    ,    ATSpecific    =3               // Specific frame resolution desired, no need to keep proportions if both width and height specified
    ,    ATLimitSizeAndKeepAspect = 4   // Keep frame proportions and reduce frame size to specified maximum size
    } EAdviceType;

    virtual EAdviceType GetAdvice(::uint32_t& width, ::uint32_t& height) const =0;

protected:
    virtual ~IFrameGeometryAdvisor(){}
};

using PFrameGeometryAdvisor = NCorbaHelpers::CAutoPtr<IFrameGeometryAdvisor>;

struct FrameGeometryAdvice
{
    IFrameGeometryAdvisor::EAdviceType Type = IFrameGeometryAdvisor::ATDontCare;
    uint32_t Width = 0u;
    uint32_t Height = 0u;
};

inline FrameGeometryAdvice GetFrameGeometryAdvice(PFrameGeometryAdvisor advisor)
{
    FrameGeometryAdvice result;
    result.Type = advisor->GetAdvice(result.Width, result.Height);
    return result;
}

inline FrameGeometryAdvice GetFrameGeometryAdvice(uint32_t width)
{
    return { IFrameGeometryAdvisor::ATSpecific, width, 0};
}

}
