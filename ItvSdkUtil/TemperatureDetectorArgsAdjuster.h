#ifndef ITVSDKUTIL_TEMPERATUREDETECTORARGSADJUSTER_H
#define ITVSDKUTIL_TEMPERATUREDETECTORARGSADJUSTER_H

#include <ItvDetectorSdk/include/ItvDetectorsSdk2.h>

#include "../MMClient/DetectorEventFactory.h"

#include "BaseEventArgsAdjuster.h"

class TemperatureDetectorArgsAdjuster : public BaseEventArgsAdjuster
{
public:
    TemperatureDetectorArgsAdjuster(DECLARE_LOGGER_ARG, const std::string& prefix, NMMSS::IDetectorEvent* event);

public:
    virtual ITV8::hresult_t SetMultimediaBuffer(const std::string& name, 
        boost::shared_ptr<ITV8::MFF::IMultimediaBuffer> multimediaBuffer);

private:
    ITV8::hresult_t processMatrix(const std::string& name, const ITV8::IFloatMatrix* matrix);

private:
    NMMSS::PDetectorEventFactory m_factory;
    ITV8::timestamp_t m_time;

};

#endif // ITVSDKUTIL_TEMPERATUREDETECTORARGSADJUSTER_H
