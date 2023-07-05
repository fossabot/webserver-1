#include "BaseEventArgsAdjuster.h"
#include <ItvFramework/TimeConverter.h>

#include <boost/mem_fn.hpp>

#include <json/json.h>

#include <ItvDetectorSdk/include/DetectorConstants.h>

#include "../MMClient/DetectorEventFactory.h"

BaseEventArgsAdjuster::BaseEventArgsAdjuster(DECLARE_LOGGER_ARG, const std::string& prefix,
                                             NMMSS::IDetectorEvent* event)
    : m_event(event)
    , m_prefix(prefix)
    , m_ignoreMessage(0)
{
    INIT_LOGGER_HOLDER;
    assert(m_event);
}

ITV8::hresult_t BaseEventArgsAdjuster::SetValue(const char* name, ITV8::bool_t val)
{
    if (!name)
        return ITV8::EInvalidParams;
    return SetValue(std::string(name), val);
}

ITV8::hresult_t BaseEventArgsAdjuster::SetValue(const char* name, ITV8::int32_t val)
{
    if (!name)
        return ITV8::EInvalidParams;
    return SetValue(std::string(name), val);
}

ITV8::hresult_t BaseEventArgsAdjuster::SetValue(const char* name, ITV8::double_t val)
{
    if (!name)
        return ITV8::EInvalidParams;
    return SetValue(std::string(name), val);
}

ITV8::hresult_t BaseEventArgsAdjuster::SetValue(const char* name, const char* val)
{
    if (!name || !val)
        return ITV8::EInvalidParams;
    return SetValue(std::string(name), std::string(val));
}

ITV8::hresult_t BaseEventArgsAdjuster::SetTimestamp(ITV8::timestamp_t val)
{
    m_event->SetTimestamp(ITV8::PtimeFromTimestamp(val));
    return ITV8::ENotError;
}

ITV8::hresult_t BaseEventArgsAdjuster::SetMultimediaBuffer(const char* name,
                                                           ITV8::MFF::IMultimediaBuffer* multimediaBuffer)
{
    if (!name || !multimediaBuffer)
        return ITV8::EInvalidParams;

    boost::shared_ptr<ITV8::MFF::IMultimediaBuffer> buff(multimediaBuffer, 
        boost::mem_fn(&ITV8::MFF::IMultimediaBuffer::Destroy));
    return SetMultimediaBuffer(std::string(name), buff);
}

ITV8::hresult_t BaseEventArgsAdjuster::SetRectangleArray(const char* name, 
                                                         ITV8::IRectangleEnumerator* collection)
{
    if (!name || !collection)
        return ITV8::EInvalidParams;
    return SetRectangleArray(std::string(name), collection);
}

ITV8::hresult_t BaseEventArgsAdjuster::SetMask(const char* name, ITV8::IMask* mask)
{
    if (!name || !mask)
        return ITV8::EInvalidParams;
    return SetMask(std::string(name), mask);
}

ITV8::hresult_t BaseEventArgsAdjuster::SetPolylineArray(const char* name, 
                                                        ITV8::IPolylineEnumerator* collection)
{
    if (!name || !collection)
        return ITV8::EInvalidParams;
    return SetPolylineArray(std::string(name), collection);
}

// Implementation part.
ITV8::hresult_t BaseEventArgsAdjuster::SetValue(const std::string& name, ITV8::bool_t val)
{
    _wrn_if_(IsFirst(eimSetValue)) << m_prefix << " SetValue(" << name << "," << val << ") not implemented."
        << std::endl;
    return ITV8::ENotError;
}

ITV8::hresult_t BaseEventArgsAdjuster::SetValue(const std::string& name, ITV8::int32_t val)
{
    if (m_event)
    {
        m_event->SetValue(name, val);
        return ITV8::ENotError;
    }
    _wrn_ << m_prefix << " setValue(" << name << ") has no effect, because of empty event";
    return ITV8::EInvalidOperation;
}

ITV8::hresult_t BaseEventArgsAdjuster::SetValue(const std::string& name, ITV8::double_t val)
{
    if (m_event)
    {
        m_event->SetValue(name, val);
        return ITV8::ENotError;
    }
    _wrn_ << m_prefix << " setValue(" << name << ") has no effect, because of empty event";
    return ITV8::EInvalidOperation;
}

ITV8::hresult_t BaseEventArgsAdjuster::SetValue(const std::string& name, const std::string& val)
{
    return setValue(name, val);
}

ITV8::hresult_t BaseEventArgsAdjuster::SetMultimediaBuffer(const std::string& name, boost::shared_ptr<ITV8::MFF::IMultimediaBuffer> multimediaBuffer)
{
    if (!strcmp(name.c_str(), "FaceDescriptor") || !strcmp(name.c_str(), "ObjectDescriptor") || !strcmp(name.c_str(), "ObjectImage"))
    {
        ITV8::MFF::IBlobBuffer* buffer = ITV8::contract_cast<ITV8::MFF::IBlobBuffer>(multimediaBuffer.get());
        if (m_event)
            m_event->SetValue(name, buffer->GetData(), buffer->GetSize());
        else
        {
            _wrn_ << m_prefix << " SetMultimediaBuffer(" << name << ",...)" << " has empty event, the changes won't be applied";
            return ITV8::EInvalidOperation;
        }
    }
    else if (!strcmp(name.c_str(), ITV8_METADATA_TYPE_TARGET_LIST) && (m_prefix == "bodyTemperature" || m_prefix == "bodyPreTemperature"))
    {
        if (m_event)
        {
            auto targetList = ITV8::contract_cast<ITV8::Analytics::ITargetEnumerator>(multimediaBuffer.get());
            if (!targetList)
            {
                _wrn_ << m_prefix << " SetMultimediaBuffer(" << name << ",...)"
                    << " has been called with invalid params. ITargetEnumerator was expected";
                return ITV8::EInvalidParams;
            }

            targetList->Reset();
            if (targetList->MoveNext())
            {
                auto target = targetList->GetCurrent();
                ITV8::RectangleF rect;
                target->GetGeometry(&rect);
                setRectangle(0, rect);

                ITV8::uint32_t jsonSize = 0;
                target->GetValue(ITV8_METADATA_PAYLOAD_TYPE_JSON, nullptr, &jsonSize);

                std::vector<char> json(jsonSize);
                target->GetValue(ITV8_METADATA_PAYLOAD_TYPE_JSON, json.data(), &jsonSize);

                Json::Value root;
                Json::CharReaderBuilder reader;
                std::string err;

                std::stringstream ss;
                ss << json.data();
                if (!Json::parseFromStream(reader, ss, &root, &err))
                {
                    _wrn_ << "SetMultimediaBuffer: can't parse json values: " << json.data() << ". Error message: " << err;
                    return ITV8::EInvalidParams;
                }

                auto convertedJson = root[ITV8_DETECTOR_ANPR_HYPOTHESES][0];
                convertedJson[ITV8_DETECTOR_EVENT_CLASS] = root[ITV8_DETECTOR_EVENT_CLASS];
                convertedJson[ITV8_DETECTOR_FACE_BEGIN_TIME] = 
                    static_cast<Json::Value::UInt64>(ITV8::PtimeToTimestamp(
                        boost::posix_time::from_iso_string(root[ITV8_DETECTOR_FACE_BEGIN_TIME].asString())));
                convertedJson[ITV8_DETECTOR_FACE_END_TIME] = 
                    static_cast<Json::Value::UInt64>(ITV8::PtimeToTimestamp(
                        boost::posix_time::from_iso_string(root[ITV8_DETECTOR_FACE_END_TIME].asString())));

                if (!convertedJson.isMember(ITV8_DETECTOR_TEMPERATURE_UNIT))
                {
                    convertedJson[ITV8_DETECTOR_TEMPERATURE_UNIT] = ITV8::Analytics::ETemperatureUnit::ECelsius;
                }

                const auto convertedJsonStr = Json::FastWriter().write(convertedJson);

                SetValue(ITV8_DETECTOR_FACE_JSON, convertedJsonStr);
            }
        }
        else
        {
            _wrn_ << m_prefix << " SetMultimediaBuffer(" << name << ",...)" << " has empty event, the changes won't be applied";
            return ITV8::EInvalidOperation;
        }
    }
    else
        _wrn_if_(IsFirst(eimSetMultimediaBuffer)) << m_prefix  << " SetMultimediaBuffer(" << name << ",...) not implemented." << std::endl;

    return ITV8::ENotError;
}

ITV8::hresult_t BaseEventArgsAdjuster::SetRectangleArray(const std::string& name, ITV8::IRectangleEnumerator* collection)
{
    _wrn_if_(IsFirst(eimSetRectangleArray)) << m_prefix  << " SetRectangleArray(" << name << ",...) not implemented."
        << std::endl;
    return ITV8::ENotError;
}

ITV8::hresult_t BaseEventArgsAdjuster::SetMask(const std::string& name, ITV8::IMask* mask)
{
    _wrn_if_(IsFirst(eimSetMask)) << m_prefix  << " SetMask(" << name << ",...) not implemented."
        << std::endl;
    return ITV8::ENotError;
}

ITV8::hresult_t BaseEventArgsAdjuster::SetPolylineArray(const std::string& name, 
                                                        ITV8::IPolylineEnumerator* collection)
{
    _wrn_if_(IsFirst(eimSetPolylineArray)) << m_prefix  << " SetPolylineArray(" << name << ",...) not implemented."
        << std::endl;
    return ITV8::ENotError;
}

NMMSS::IDetectorEvent* BaseEventArgsAdjuster::event() const
{
    return m_event;
}

const std::string& BaseEventArgsAdjuster::prefix() const
{
    return m_prefix;
}

ITV8::hresult_t BaseEventArgsAdjuster::setRectangle(ITV8::uint32_t id, const ITV8::RectangleF& rectangle, NMMSS::IDetectorEvent* event)
{
    NMMSS::IDetectorEvent* const realEvent = event ? event : this->event();

    if (rectangle.left   < 0.0 || rectangle.left   > 1.0 ||
        rectangle.top    < 0.0 || rectangle.top    > 1.0 ||
        rectangle.height < 0.0 || rectangle.height > 1.0 ||
        rectangle.width  < 0.0 || rectangle.width  > 1.0)
    {
        _err_ << prefix() << " RectangleF has invalid value={"
            << rectangle.left  << ", " << rectangle.top     << ", "
            << rectangle.width << ", " << rectangle.height  << "}"
            << std::endl;
        return ITV8::EValueOutOfRange;
    }

    if (realEvent)
    {
        realEvent->SetRectangle(id, rectangle.left, rectangle.top, rectangle.left + rectangle.width, rectangle.top + rectangle.height);
        return ITV8::ENotError;
    }
    _wrn_ << m_prefix << " setRectangle(" << id << ") has no effect, because of empty event";
    return ITV8::EInvalidOperation;
}

ITV8::hresult_t BaseEventArgsAdjuster::setValue(const std::string& name, const std::string& val, NMMSS::IDetectorEvent* event)
{
    NMMSS::IDetectorEvent* const realEvent = event ? event : this->event();
    if (realEvent)
    {
        realEvent->SetValue(name, val);
        return ITV8::ENotError;
    }
    _wrn_ << m_prefix << " setValue(" << name << ") has no effect, because of empty event";
    return ITV8::EInvalidOperation;
}