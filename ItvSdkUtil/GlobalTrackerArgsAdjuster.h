#pragma once

#include "BaseEventArgsAdjuster.h"

class GlobalTrackerArgsAdjuster: public BaseEventArgsAdjuster
{
public:
    GlobalTrackerArgsAdjuster(DECLARE_LOGGER_ARG, const std::string& prefix, NMMSS::IDetectorEvent* event)
        : BaseEventArgsAdjuster(GET_LOGGER_PTR, prefix, event)
    {
    }

    ITV8::hresult_t SetMultimediaBuffer(const std::string& name,
        boost::shared_ptr<ITV8::MFF::IMultimediaBuffer> multimediaBuffer) override;
};

