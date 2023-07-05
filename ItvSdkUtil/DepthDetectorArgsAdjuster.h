#ifndef ITVSDKUTIL_DEPTHDETECTORARGSADJUSTER_H
#define ITVSDKUTIL_DEPTHDETECTORARGSADJUSTER_H

#include <ItvDetectorSdk/include/ItvDetectorsSdk2.h>

#include "../MMClient/DetectorEventFactory.h"

#include "BaseEventArgsAdjuster.h"

class DepthDetectorArgsAdjuster : public BaseEventArgsAdjuster
{
    typedef std::vector<char> maskBuffer_t;

public:
    DepthDetectorArgsAdjuster(DECLARE_LOGGER_ARG, const std::string& prefix, NMMSS::IDetectorEvent* event)
        : BaseEventArgsAdjuster(GET_LOGGER_PTR, prefix, event)
    {
    }

public:
    ITV8::hresult_t SetMask(const char* name, ITV8::IMask* mask) override
    {
        if (mask == nullptr)
        {
            return ITV8::EInvalidParams;
        }

        auto h = mask->GetSize().height;
        auto w = mask->GetSize().width;

        event()->SetValue("Height", h);
        event()->SetValue("Width", w);
        event()->SetValue(name, mask->GetMask(), h * w);

        return ITV8::ENotError;
    }
};

#endif // ITVSDKUTIL_DEPTHDETECTORARGSADJUSTER_H
