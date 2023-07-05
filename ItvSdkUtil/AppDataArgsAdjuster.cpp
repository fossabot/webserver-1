#include "AppDataArgsAdjuster.h"

#include <vector>

#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/optional/optional.hpp>

#include <ItvDetectorSdk/include/DetectorConstants.h>
#include <ItvFramework/TimeConverter.h>

#include <Logging/Log3.h>

#include <MMIDL/MMVideoC.h>
#include "../MMCoding/CoordinateTransform.h"

#include "CFaceTrackerWrap.h"

// Hello to Microsoft
#ifdef CreateEvent
#   undef CreateEvent
#endif

namespace
{
typedef boost::optional<std::string> mayBeString_t;
mayBeString_t getObjectClass(const ITV8::IPropertiesGetter* propGetter)
{
    static const ITV8::uint32_t TARGET_PROPERTY_BUFFER_SIZE = 256;
    std::vector<char> buff(TARGET_PROPERTY_BUFFER_SIZE);
    ITV8::uint32_t buffSize = TARGET_PROPERTY_BUFFER_SIZE;
    ITV8::hresult_t err = propGetter->GetValue(ITV8_DETECTOR_OBJECT_CLASS, &buff[0], &buffSize);
    if (ITV8::EBufferTooSmall == err)
    {
        buff.resize(buffSize);
        err = propGetter->GetValue(ITV8_DETECTOR_OBJECT_CLASS, &buff[0], &buffSize);
    }
    if (ITV8::ENotError != err || 0 == buffSize)
    {
        return boost::optional<std::string>();
    }
    // Cut off last '\0'.
    return std::string(&buff[0], buffSize-1);
}

boost::optional<int> mapObjectClass(const std::string& objectClass)
{
    // TODO: enum for server and client
    if( objectClass == ITV8_DETECTOR_OBJECT_CLASS_FACE )
        return ITV8::Analytics::EFaceObject;
    else if( objectClass == ITV8_DETECTOR_OBJECT_CLASS_HUMAN )
        return ITV8::Analytics::EHumanObject;
    else if( objectClass == ITV8_DETECTOR_OBJECT_CLASS_GROUP )
        return ITV8::Analytics::EGroupOfHumansObject;
    else if( objectClass == ITV8_DETECTOR_OBJECT_CLASS_VEHICLE )
        return ITV8::Analytics::EVehicleObject;
    else
        return boost::none;
}

bool isFace(const ITV8::IContract* obj)
{
    const ITV8::IPropertiesGetter* const propGetter = ITV8::contract_cast<ITV8::IPropertiesGetter>(obj);
    if (propGetter)
    {
        const mayBeString_t objectClass = getObjectClass(propGetter);
        if (objectClass && ITV8_DETECTOR_OBJECT_CLASS_FACE == objectClass.get())
        {
            return true;
        }
    }
    return false;
}
}

AppDataArgsAdjuster::AppDataArgsAdjuster(DECLARE_LOGGER_ARG, const std::string& prefix,
                                     NMMSS::IDetectorEvent* event,
                                     NMMSS::PDetectorEventFactory factory,
                                     ITV8::timestamp_t time,
                                     boost::shared_ptr<CFaceTrackerWrap> faceTrackerWrap)
    : BaseEventArgsAdjuster(GET_LOGGER_PTR, prefix, event)
    , m_factory(factory)
    , m_time(time)
    , m_faceTrackerWrap(faceTrackerWrap)
{}

ITV8::hresult_t AppDataArgsAdjuster::SetMultimediaBuffer(const std::string& name,
                                                         boost::shared_ptr<ITV8::MFF::IMultimediaBuffer> multimediaBuffer)
{
    ITV8::Analytics::ITargetEnumerator* const targetList = 
        ITV8::contract_cast<ITV8::Analytics::ITargetEnumerator>(multimediaBuffer.get());

    if (!targetList)
    {
        return BaseEventArgsAdjuster::SetMultimediaBuffer(name, multimediaBuffer);
    }

    return iterateEnumerator(targetList, boost::bind(&AppDataArgsAdjuster::processTarget, this, _1));
}

ITV8::hresult_t AppDataArgsAdjuster::SetRectangleArray(const std::string& name,
                                                       ITV8::IRectangleEnumerator* collection)
{
    std::vector<ITV8::RectangleF> rects;
    ITV8::hresult_t res = iterateEnumerator(collection,
        boost::bind(&AppDataArgsAdjuster::processRectangle, this, _1, boost::ref(rects)));

    if (ITV8::ENotError != res)
    {
        return res;
    }

    typedef CFaceTrackerWrap::TTrackMap TTrackMap;

    TTrackMap disappTracks;
    if (rects.empty())
    {
        m_faceTrackerWrap->ForceFinishTracks(disappTracks);
    }
    else
    {
        m_faceTrackerWrap->OnFrame(m_time, rects);

        TTrackMap appTracks, currentTracks;
        m_faceTrackerWrap->GetCurrentTracks(appTracks, currentTracks, disappTracks);

        const bool isFaceCollection = isFace(collection);
        BOOST_FOREACH(TTrackMap::const_reference face, appTracks)
        {
            setRectangle(static_cast<ITV8::uint32_t>(face.first), face.second);
            if (isFaceCollection)
            {
                onFaceAppeared(static_cast<ITV8::uint32_t>(face.first), face.second);
            }
        }

        BOOST_FOREACH(TTrackMap::const_reference face, currentTracks)
        {
            setRectangle(static_cast<ITV8::uint32_t>(face.first), face.second);
        }
    }

    BOOST_FOREACH(TTrackMap::const_reference face, disappTracks)
    {
        event()->RemoveRectangle(static_cast<ITV8::uint32_t>(face.first));
    }

    return ITV8::ENotError;
}

ITV8::hresult_t AppDataArgsAdjuster::SetMask(const std::string& name, ITV8::IMask* mask)
{
    const ITV8::Size maskSize = mask->GetSize();
    const ITV8::uint8_t* const maskBegin = mask->GetMask();
    const ITV8::uint8_t* const maskEnd = maskBegin + maskSize.width * maskSize.height;
    const ITV8::uint8_t* const activeMaskCell = std::find_if(maskBegin, maskEnd, std::bind1st(std::not_equal_to<ITV8::uint8_t>(), 0));

    event()->SetScalar(1, activeMaskCell == maskEnd ? -1.0 : 1.0);

    return ITV8::ENotError;
}

ITV8::hresult_t AppDataArgsAdjuster::processTarget(const ITV8::Analytics::ITarget* target)
{
    ITV8::hresult_t err;
    ITV8::RectangleF rectangle;
    err = target->GetGeometry(&rectangle);
    if (err != ITV8::ENotError)
    {
        _err_ << prefix() << " target->GetGeometry(...) return " << err << "." << std::endl;
        return ITV8::EInvalidParams;
    }

    ITV8::uint32_t stateValue;
    err = target->GetState(&stateValue);
    if (err != ITV8::ENotError)
    {
        _err_ << prefix() << " target->GetState(...) return " << err << "." << std::endl;
        return ITV8::EInvalidParams;
    }

    const ITV8::Analytics::ObjectHandle_t targetId = target->GetId();
    ITV8::Analytics::ETargetState state = ITV8::Analytics::ETargetState(stateValue);
    if (ITV8::Analytics::Disappeared == state || ITV8::Analytics::Revoked == state)
    {
        event()->RemoveRectangle(targetId);
    }
    else
    {
        err = setRectangle(targetId, rectangle);
        if (err != ITV8::ENotError)
        {
            return err;
        }

        const ITV8::IPropertiesGetter* propertiesGetter = ITV8::contract_cast<ITV8::IPropertiesGetter>(target);
        if (propertiesGetter)
        {
            ITV8::double_t colorH = 0;
            ITV8::double_t colorS = 0;
            ITV8::double_t colorV = 0;

            if (propertiesGetter->GetValue(ITV8_DETECTOR_OBJECT_COLOR_H, &colorH) == ITV8::ENotError &&
                propertiesGetter->GetValue(ITV8_DETECTOR_OBJECT_COLOR_S, &colorS) == ITV8::ENotError &&
                propertiesGetter->GetValue(ITV8_DETECTOR_OBJECT_COLOR_V, &colorV) == ITV8::ENotError)
            {
                event()->SetColor(targetId, colorH, colorS, colorV);
            }

            ITV8::int32_t objectClassType = 0;
            if (propertiesGetter->GetValue(ITV8_DETECTOR_OBJECT_CLASS_TYPE, &objectClassType) == ITV8::ENotError)
            {
                event()->SetObjectClass(targetId, objectClassType);
            }
            // Legacy
            else if (mayBeString_t objectClass = getObjectClass(propertiesGetter))
            {
                if (boost::optional<int> mappedObjectType = mapObjectClass(objectClass.get()))
                    event()->SetObjectClass(targetId, mappedObjectType.get());
            }

            ITV8::int32_t behavior = 0;
            if (propertiesGetter->GetValue(ITV8_DETECTOR_OBJECT_BEHAVIOR_TYPE, &behavior) == ITV8::ENotError)
            {
                event()->SetBehavior(targetId, behavior);
            }

            ITV8::int32_t evasionType = 0;
            if (propertiesGetter->GetValue(ITV8_DETECTOR_FACE_EVASION_TYPE, &evasionType) == ITV8::ENotError)
            {
                event()->SetValue(targetId, ITV8_DETECTOR_FACE_EVASION_TYPE, evasionType);
            }

            ITV8::double_t centerX = 0;
            ITV8::double_t centerY = 0;
            if (propertiesGetter->GetValue(ITV8_DETECTOR_OBJECT_CENTER_X, &centerX) == ITV8::ENotError &&
                propertiesGetter->GetValue(ITV8_DETECTOR_OBJECT_CENTER_Y, &centerY) == ITV8::ENotError)
            {
                event()->SetCenter(targetId, centerX, centerY);
            }

            ITV8::double_t temperatureValue = 0;
            ITV8::int32_t temperatureUnit;
            if (   propertiesGetter->GetValue("TemperatureValue", &temperatureValue) == ITV8::ENotError
                && propertiesGetter->GetValue("TemperatureUnit", &temperatureUnit) == ITV8::ENotError)
            {
                event()->SetTemperature(targetId, temperatureValue, temperatureUnit);
            }
        }

        ITV8::int32_t width;
        ITV8::int32_t height;
        if (   target->GetValue(ITV8_DETECTOR_FRAME_WIDTH, &width) == ITV8::ENotError
            && target->GetValue(ITV8_DETECTOR_FRAME_HEIGHT, &height) == ITV8::ENotError)
        {
            event()->SetValue(targetId, ITV8_DETECTOR_FRAME_WIDTH, width);
            event()->SetValue(targetId, ITV8_DETECTOR_FRAME_HEIGHT, height);
        }

        if (auto* bestVisibilityMoment = ITV8::contract_cast<ITV8::Analytics::IBestVisibilityMoment>(target))
        {
            ITV8::RectangleF bestVisibilityRect;
            ITV8::timestamp_t bestVisibilityTimestamp;
            if (   bestVisibilityMoment->GetBestVisibilityRect(&bestVisibilityRect) == ITV8::ENotError
                && bestVisibilityMoment->GetBestVisibilityTimestamp(&bestVisibilityTimestamp) == ITV8::ENotError)
            {
                if (bestVisibilityRect.left   < 0.0 || bestVisibilityRect.left   > 1.0 ||
                    bestVisibilityRect.top    < 0.0 || bestVisibilityRect.top    > 1.0 ||
                    bestVisibilityRect.height < 0.0 || bestVisibilityRect.height > 1.0 ||
                    bestVisibilityRect.width  < 0.0 || bestVisibilityRect.width  > 1.0)
                {
                    _errf_("{0} RectangleF has invalid value={{{0}, {1}, {2}, {4}}}",
                           prefix(),
                           bestVisibilityRect.left,
                           bestVisibilityRect.top,
                           bestVisibilityRect.width,
                           bestVisibilityRect.height);
                    return ITV8::EValueOutOfRange;
                }

                auto r = transformRectangle(bestVisibilityRect);
                event()->SetBestVisibilityRect(target->GetId(), r.left, r.top, r.left + r.width, r.top + r.height);
                event()->SetBestVisibilityTimestamp(target->GetId(), bestVisibilityTimestamp);
            }
        }

        // Special case for face tracker.
        if (isFace(target))
        {
            if (ITV8::Analytics::Appeared == state)
            {
                static const ITV8::uint32_t TARGET_UUID_PROPERTY_SIZE = 37;
                std::vector<char> buff(TARGET_UUID_PROPERTY_SIZE);
                ITV8::uint32_t size = TARGET_UUID_PROPERTY_SIZE;
                ITV8::hresult_t res = target->GetValue(ITV8_DETECTOR_FACE_UUID, &buff[0], &size);
                if (ITV8::ENotError == res)
                {
                    ////////////// ACR-27013
                    // means that is tva detector face but the event should pass from detector pack
                    // std::string faceId;
                    // for (auto c : buff) faceId.append(1, c);
                    // onFaceAppeared(target->GetId(), rectangle, faceId.c_str());
                    ////////////////////////
                }
                else
                    onFaceAppeared(target->GetId(), rectangle);
            }
        }
    }

    return ITV8::ENotError;
}

ITV8::hresult_t AppDataArgsAdjuster::processRectangle(const ITV8::IRectangle* rect, std::vector<ITV8::RectangleF>& rects)
{
    rects.push_back(rect->GetRectangle());
    return ITV8::ENotError;
}

void AppDataArgsAdjuster::onFaceAppeared(ITV8::uint32_t id, const ITV8::RectangleF& faceRect, const char* uniqueFaceId)
{
    NMMSS::IDetectorEvent* ev = m_factory->CreateEvent(MMSS::DetectorEventTypes::DET_FaceAppeared, NMMSS::AS_Happened, ITV8::PtimeFromTimestamp(m_time));
    setRectangle(id, faceRect, ev);
    if (0 != uniqueFaceId)
        setValue("FaceId", uniqueFaceId, ev);
    ev->Commit();
}

ITV8::hresult_t AppDataArgsAdjuster::setRectangle(ITV8::uint32_t id, const ITV8::RectangleF& inRectangle, NMMSS::IDetectorEvent* event)
{
    return BaseEventArgsAdjuster::setRectangle(id, transformRectangle(inRectangle), event);
}
ITV8::RectangleF AppDataArgsAdjuster::transformRectangle(const ITV8::RectangleF& inRectangle)
{
    ITV8::RectangleF rectangle = inRectangle;
    if (auto transform = m_factory->GetCoordinateTransform())
    {
        auto rect = transform->Transform({ {rectangle.left, rectangle.top}, {rectangle.left + rectangle.width, rectangle.top + rectangle.height} });
        rectangle = { rect.first.first, rect.first.second, rect.second.first - rect.first.first, rect.second.second - rect.first.second };
    }
    return rectangle;
}
