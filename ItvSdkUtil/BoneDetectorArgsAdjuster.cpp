#include "BoneDetectorArgsAdjuster.h"

#include <vector>

#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/optional/optional.hpp>
#include <boost/algorithm/string.hpp>

#include <ItvDetectorSdk/include/DetectorConstants.h>
#include <ItvFramework/TimeConverter.h>

#include <MMIDL/MMVideoC.h>

#include "CFaceTrackerWrap.h"

// Hello to Microsoft
#ifdef CreateEvent
#   undef CreateEvent
#endif

BoneDetectorArgsAdjuster::BoneDetectorArgsAdjuster(DECLARE_LOGGER_ARG, const std::string& prefix,
                                                   NMMSS::IDetectorEvent* event,
                                                   NMMSS::PDetectorEventFactory factory,
                                                   ITV8::timestamp_t time)
    : BaseEventArgsAdjuster(GET_LOGGER_PTR, prefix, event)
    , m_factory(factory)
    , m_time(time)
{
    std::string pointNamesStr(ITV8_METADATA_HUMAN_BONE_POINTS);
    boost::trim_if(pointNamesStr, boost::is_any_of(";"));
    boost::split(m_pointNames, pointNamesStr, boost::is_any_of(";"));
}

ITV8::hresult_t BoneDetectorArgsAdjuster::SetMultimediaBuffer(const std::string& name,
                                                         boost::shared_ptr<ITV8::MFF::IMultimediaBuffer> multimediaBuffer)
{
    ITV8::Analytics::ITargetEnumerator* const targetList = 
        ITV8::contract_cast<ITV8::Analytics::ITargetEnumerator>(multimediaBuffer.get());

    if (!targetList)
    {
        return BaseEventArgsAdjuster::SetMultimediaBuffer(name, multimediaBuffer);
    }

    return iterateEnumerator(targetList, boost::bind(&BoneDetectorArgsAdjuster::processTarget, this, _1));
}

ITV8::hresult_t BoneDetectorArgsAdjuster::processTarget(const ITV8::Analytics::ITarget* target)
{
    if (!target)
    {
        return ITV8::EInvalidParams;
    }

    ITV8::hresult_t err;
    ITV8::uint32_t stateValue;
    err = target->GetState(&stateValue);
    if (err == ITV8::ENotError)
    {
        ITV8::Analytics::ETargetState state = ITV8::Analytics::ETargetState(stateValue);
        if (ITV8::Analytics::Disappeared == state) {
            return ITV8::ENotError;
        }
    }

    ITV8::int32_t width;
    ITV8::int32_t height;

    err = target->GetValue(ITV8_DETECTOR_FRAME_WIDTH, &width);
    if (err != ITV8::ENotError)
    {
        return ITV8::EInvalidParams;
    }

    err = target->GetValue(ITV8_DETECTOR_FRAME_HEIGHT, &height);
    if (err != ITV8::ENotError)
    {
        return ITV8::EInvalidParams;
    }

    const ITV8::Analytics::ObjectHandle_t targetId = target->GetId();

    event()->SetValue(targetId, ITV8_DETECTOR_FRAME_WIDTH, width);
    event()->SetValue(targetId, ITV8_DETECTOR_FRAME_HEIGHT, height);

    bool targetHasNoPoints = true;
    for (const auto &pointName : m_pointNames)
    {
        ITV8::PointF point;
        const auto errX = target->GetValue((pointName + "_X").c_str(), &point.x);
        const auto errY = target->GetValue((pointName + "_Y").c_str(), &point.y);

        if (errX == ITV8::ENotError && errY == ITV8::ENotError)
        {
            targetHasNoPoints = false;
            event()->SetPoint(targetId, pointName, point.x, point.y);
        }
    }

    return targetHasNoPoints ? ITV8::EInvalidParams : ITV8::ENotError;
}
