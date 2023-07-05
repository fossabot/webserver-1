#ifndef ITVSDKUTIL_BONEDETECTORARGSADJUSTER_H
#define ITVSDKUTIL_BONEDETECTORARGSADJUSTER_H

#include <ItvDetectorSdk/include/ItvDetectorsSdk2.h>

#include "../MMClient/DetectorEventFactory.h"

#include "BaseEventArgsAdjuster.h"

class BoneDetectorArgsAdjuster : public BaseEventArgsAdjuster
{
public:
    BoneDetectorArgsAdjuster(DECLARE_LOGGER_ARG, const std::string& prefix, NMMSS::IDetectorEvent* event,
        NMMSS::PDetectorEventFactory factory,
        ITV8::timestamp_t time);

public:
    virtual ITV8::hresult_t SetMultimediaBuffer(const std::string& name, 
        boost::shared_ptr<ITV8::MFF::IMultimediaBuffer> multimediaBuffer);

private:
    ITV8::hresult_t processTarget(const ITV8::Analytics::ITarget* target);

private:
    NMMSS::PDetectorEventFactory m_factory;
    ITV8::timestamp_t m_time;
    std::vector<std::string> m_pointNames;

};

#endif // ITVSDKUTIL_BONEDETECTORARGSADJUSTER_H
