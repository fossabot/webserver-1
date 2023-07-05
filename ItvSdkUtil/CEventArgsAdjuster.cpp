#include "CEventArgsAdjuster.h"
#include "../MMClient/DetectorEventFactory.h"
#include <ItvDetectorSdk/include/DetectorConstants.h>
#include <boost/mem_fn.hpp>
#include <ItvFramework/TimeConverter.h>

CEventArgsAdjuster::CEventArgsAdjuster(DECLARE_LOGGER_ARG, const std::string& prefix,
                                       NMMSS::IDetectorEvent* event)
    :m_event(event), m_prefix(prefix), m_contourId(0), m_detectorId(0), m_ignoreMessage((EIgnoreMessage)0)
{
    INIT_LOGGER_HOLDER;
}

CEventArgsAdjuster::CEventArgsAdjuster(DECLARE_LOGGER_ARG, const std::string& prefix,
                                       NMMSS::IDetectorEvent* event
                                       , boost::weak_ptr<NMMSS::IDetectorEventFactory> factory
                                       , short detectorId, const ITV8::timestamp_t& time
                                       , PFaceTrackerWrap faceTrackerWrap)
    :m_event(event), m_prefix(prefix), m_contourId(0), m_factory(factory), m_detectorId(detectorId), m_time(time), m_faceTrackerWrap(faceTrackerWrap), m_ignoreMessage((EIgnoreMessage)0)
{
    INIT_LOGGER_HOLDER;
}

ITV8::hresult_t CEventArgsAdjuster::SetValue(const char* name, ITV8::bool_t val)
{
    _wrn_if_(IsFirst(eimSetValue)) << m_prefix << " SetValue("<<name<<","<<val<<") not implemented."
        <<std::endl;
    return ITV8::ENotError;
}

ITV8::hresult_t CEventArgsAdjuster::SetValue(const char* name, ITV8::int32_t val)
{
    _wrn_if_(IsFirst(eimSetValue)) << m_prefix  << " SetValue("<<name<<","<<val<<") not implemented."
        <<std::endl;
    return ITV8::ENotError;
}

ITV8::hresult_t CEventArgsAdjuster::SetValue(const char* name, ITV8::double_t val)
{
    _wrn_if_(IsFirst(eimSetValue)) << m_prefix  << " SetValue("<<name<<","<<val<<") not implemented."
        <<std::endl;
    return ITV8::ENotError;
}

ITV8::hresult_t CEventArgsAdjuster::SetValue(const char* name, const char* val)
{
    _wrn_if_(IsFirst(eimSetValue)) << m_prefix  << " SetMask("<<name<<","<<val<<") not implemented."
        <<std::endl;
    return ITV8::ENotError;
}

ITV8::hresult_t CEventArgsAdjuster::SetMultimediaBuffer(const char* name, 
                                                        ITV8::MFF::IMultimediaBuffer* multimediaBuffer)
{
    if (0 == m_event)
        return ITV8::EUnsupportedCommand;

    boost::shared_ptr<ITV8::MFF::IMultimediaBuffer> buff(multimediaBuffer, 
        boost::mem_fn(&ITV8::MFF::IMultimediaBuffer::Destroy));
    ITV8::Analytics::ITargetEnumerator *targetList = 
        ITV8::contract_cast<ITV8::Analytics::ITargetEnumerator>(multimediaBuffer);
    if(targetList != 0)
    {
        if(strcmp(name, ITV8_DEFAULT_METADATA_PROPERTY_NAME))
        {
            // IsFirst(eimDefaultPropName) нифига не работает, т.к. CEventArgsAdjuster создается на каждое событие.
            //_wrn_if_(IsFirst(eimDefaultPropName)) << m_prefix  << "Specified property name \"" << 
            //    name  << "\" instead of expected \"" << ITV8_DEFAULT_METADATA_PROPERTY_NAME 
            //    << "\" for metadata of TargetList." << std::endl;
        }
        targetList->Reset();
        std::vector<ITV8::RectangleF> rects;
        unsigned short targetsCount = 0;
        unsigned short disappTargetsCount = 0;
        while(targetList->MoveNext())
        {
            ITV8::uint32_t stateValue;
            ITV8::RectangleF rectangle;
            ITV8::hresult_t err;

            const ITV8::Analytics::ITarget *target = targetList->GetCurrent();
            
            char buff[TARGET_PROPERTY_BUFFER_SIZE];
            ITV8::uint32_t buffSize = (ITV8::uint32_t)(sizeof(buff) - 1);

            err = target->GetValue(ITV8_DETECTOR_OBJECT_CLASS, buff, &buffSize);

            err = target->GetGeometry(&rectangle);
            if(err != ITV8::ENotError)
            {
                _err_ << m_prefix << " target->GetGeometry(&rectangle) return " << err << "." << std::endl;
                return ITV8::EInvalidParams;
            }

            err = target->GetState(&stateValue);
            if(err != ITV8::ENotError)
            {
                _err_ << m_prefix  << " target->GetState(&stateValue) return " << err << "." << std::endl;
                return ITV8::EInvalidParams;
            }

            if(!strcmp((const char*)buff, ITV8_DETECTOR_OBJECT_CLASS_FACE))  // если target от детектора лиц
            {
                ++targetsCount;
                ITV8::Analytics::ETargetState state = (ITV8::Analytics::ETargetState)stateValue;
                if(state == ITV8::Analytics::Disappeared || state == ITV8::Analytics::Revoked)
                    ++disappTargetsCount;
                rects.push_back(rectangle);
            }
            else // target НЕ от детектора лиц
            {
                ITV8::Analytics::ETargetState state = (ITV8::Analytics::ETargetState)stateValue;
                if(state == ITV8::Analytics::Disappeared || state == ITV8::Analytics::Revoked)
                    m_event->RemoveRectangle(target->GetId());
                else
                {  
                    err = SetRectangle(target->GetId(), rectangle);
                    if( err != ITV8::ENotError )
                        return err;
                }
            }
        }
        
        if (!rects.empty())
        {
            TTrackMap disappTracks;
            TTrackMapIterator it1, it2;
            if(disappTargetsCount == targetsCount) // все лица (по факту последнее) исчезли со сцены
            {
                m_faceTrackerWrap->ForceFinishTracks(disappTracks);
            }
            else
            {
                m_faceTrackerWrap->OnFrame(m_time, rects);

                TTrackMap appTracks, currentTracks;
                m_faceTrackerWrap->GetCurrentTracks(appTracks, currentTracks, disappTracks);

                it1 = appTracks.begin(), it2 = appTracks.end();
                for(; it1 != it2; ++it1)
                {
                    if((int)it1->first > m_faceTrackerWrap->GetLastTrackId())
                    {
                        static const char FACE_APPEARED_EVENT_NAME[] = "faceAppeared";
                        boost::shared_ptr<NMMSS::IDetectorEventFactory> factory(m_factory.lock());
                        if (0 != factory.get())
                        {
                            NMMSS::IDetectorEvent* ev = factory->CreateEvent(m_detectorId, FACE_APPEARED_EVENT_NAME, NMMSS::AS_Happened, ITV8::PtimeFromTimestamp(m_time));
                            ev->SetRectangle(it1->first, it1->second.left, it1->second.top, it1->second.left+it1->second.width, it1->second.top+it1->second.height);
                            ev->Commit();
                        }
                        m_faceTrackerWrap->LastTrackIdInc();
                        m_event->SetRectangle(it1->first, it1->second.left, it1->second.top, it1->second.left+it1->second.width, it1->second.top+it1->second.height);
                    }
                }

                it1 = currentTracks.begin(), it2 = currentTracks.end();
                for (; it1 != it2; ++it1)
                {
                    m_event->SetRectangle(it1->first, it1->second.left, it1->second.top, it1->second.left+it1->second.width, it1->second.top+it1->second.height);
                }
            }

            it1 = disappTracks.begin(), it2 = disappTracks.end();
            for (; it1 != it2; ++it1)
            {
                m_event->RemoveRectangle(it1->first);
            }
        }
    }
    else
    {
        _wrn_if_(IsFirst(eimUnsupportedMultimediaBuffer)) << m_prefix  << 
            " SetMultimediaBuffer supports only ITV8::Analytics::ITargetEnumerator type."<<std::endl;
        return ITV8::EInternalError;
    }
    return ITV8::ENotError;
}

// Sets the rectangles array as property. All rectangles should be full adjusted
// accorded to metadata file.
// Parameters:
// name - Specifies the name of property which contains rectangles array.
// collection - Rectangles array which will be set.
ITV8::hresult_t CEventArgsAdjuster::SetRectangleArray(const char* name, 
                                                      ITV8::IRectangleEnumerator* collection)
{
    collection->Reset();
    while(collection->MoveNext())
    {
        ITV8::IRectangle *rectangle = collection->GetCurrent();
        if(0 == rectangle)
        {
            _err_ << m_prefix  << " IRectangleEnumerator::GetCurrent() return 0."<<std::endl;
            return ITV8::EInvalidParams;
        }

        ITV8::hresult_t err = SetRectangle(m_contourId++, rectangle->GetRectangle());
        if(err != ITV8::ENotError)
        {
            return err;
        }
    }
    return ITV8::ENotError;
}

// Sets the mask as property. Mask should be full adjusted
// accorded to metadata file.
// Parameters:
// name - Specifies the name of property which contains mask.
// mask - Mask which will be set.
ITV8::hresult_t CEventArgsAdjuster::SetMask(const char* name, ITV8::IMask* mask)
{
    const ITV8::Size maskSize = mask->GetSize();
    const ITV8::uint8_t* const maskBegin = mask->GetMask();
    const ITV8::uint8_t* const maskEnd = maskBegin + maskSize.width * maskSize.height;
    const ITV8::uint8_t* const activeMaskCell = std::find_if(maskBegin, maskEnd, std::bind1st(std::not_equal_to<ITV8::uint8_t>(), 0));

    m_event->SetScalar(0, m_detectorId);
    m_event->SetScalar(1, activeMaskCell == maskEnd ? -1.0 : 1.0);

    return ITV8::ENotError;
}

// Sets the polylines array as property. All polylines should be full adjusted
// accorded to metadata file.
// Parameters:
// name - Specifies the name of property which contains polylines array.
// collection - Polylines array which will be set.
ITV8::hresult_t CEventArgsAdjuster::SetPolylineArray(const char* name, 
                                                     ITV8::IPolylineEnumerator* collection)
{
    _wrn_if_(IsFirst(eimSetPolylineArray)) << m_prefix  << " SetPolylineArray("<<name<<",...) not implemented."
        <<std::endl;

    return ITV8::ENotError;
}

ITV8::hresult_t CEventArgsAdjuster::SetRectangle(ITV8::uint32_t id, const ITV8::RectangleF& rectangle)
{
    if(rectangle.left < 0 || rectangle.left > 1.0 || 
        rectangle.top < 0 || rectangle.top > 1.0 || 
        rectangle.height < 0 || rectangle.height > 1.0 || 
        rectangle.width < 0 || rectangle.width > 1.0)
    {
        _err_ << m_prefix  << " RectangleF has invalid value={"
            << rectangle.left<<", "<< rectangle.top <<", "
            << rectangle.width <<", "<< rectangle.height<<"}"<<std::endl;
        return ITV8::EValueOutOfRange;
    }

    if (0 == m_event)
        return ITV8::EUnsupportedCommand;

    m_event->SetRectangle(id, rectangle.left, rectangle.top, 
        rectangle.left+rectangle.width, rectangle.top+rectangle.height);

    return ITV8::ENotError;
}
