#if !defined(ITVSDKUTIL_CEVENTARGSADJUSTER_H)
#define ITVSDKUTIL_CEVENTARGSADJUSTER_H

#include <ItvDetectorSdk/include/ItvDetectorsSdk2.h>
#include <Logging/log2.h>
#include <string>

#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include "../MMClient/DetectorEventFactory.h"
#include "CDetectorEventFactory.h"

namespace NMMSS{class IDetectorEvent;}

const int TARGET_PROPERTY_BUFFER_SIZE = 256;

class CEventArgsAdjuster : public ITV8::Analytics::IEventArgsAdjuster
{
private:
    // ITV8::IContract implementation
    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY(ITV8::IContract)
        ITV8_CONTRACT_ENTRY(ITV8::ISyncAdjuster)
        ITV8_CONTRACT_ENTRY(ITV8::Analytics::IVisualSyncAdjuster)
        ITV8_CONTRACT_ENTRY(ITV8::Analytics::IEventArgsAdjuster)
    ITV8_END_CONTRACT_MAP()

    DECLARE_LOGGER_HOLDER;

public:
    CEventArgsAdjuster(DECLARE_LOGGER_ARG, const std::string& prefix, NMMSS::IDetectorEvent* event);
    CEventArgsAdjuster(DECLARE_LOGGER_ARG, const std::string& prefix, NMMSS::IDetectorEvent* event
        , boost::weak_ptr<NMMSS::IDetectorEventFactory> factory, short detectorId, const ITV8::timestamp_t& time, PFaceTrackerWrap faceTrackerWrap);

// ITV8::Analytics::IEventArgsAdjuster implementation
public:
    virtual ITV8::hresult_t SetValue(const char* name, ITV8::bool_t val);
    virtual ITV8::hresult_t SetValue(const char* name, ITV8::int32_t val);
    virtual ITV8::hresult_t SetValue(const char* name, ITV8::double_t val);
    virtual ITV8::hresult_t SetValue(const char* name, const char* val);

    virtual ITV8::hresult_t SetMultimediaBuffer(const char* name, 
        ITV8::MFF::IMultimediaBuffer* multimediaBuffer);

    // Sets the rectangles array as property. All rectangles should be full adjusted
    // accorded to metadata file.
    // Parameters:
    // name - Specifies the name of property which contains rectangles array.
    // collection - Rectangles array which will be set.
    virtual ITV8::hresult_t SetRectangleArray(const char* name, ITV8::IRectangleEnumerator* collection);

    // Sets the mask as property. Mask should be full adjusted
    // accorded to metadata file.
    // Parameters:
    // name - Specifies the name of property which contains mask.
    // mask - Mask which will be set.
    virtual ITV8::hresult_t SetMask(const char* name, ITV8::IMask* mask);

    // Sets the polylines array as property. All polylines should be full adjusted
    // accorded to metadata file.
    // Parameters:
    // name - Specifies the name of property which contains polylines array.
    // collection - Polylines array which will be set.
    virtual ITV8::hresult_t SetPolylineArray(const char* name, ITV8::IPolylineEnumerator* collection);

private:
    NMMSS::IDetectorEvent* m_event;
    std::string m_prefix;
    ITV8::uint32_t m_contourId;
    boost::weak_ptr<NMMSS::IDetectorEventFactory> m_factory;
    short m_detectorId;
    ITV8::timestamp_t m_time;
    PFaceTrackerWrap m_faceTrackerWrap;

    typedef std::map<ITV8::uint64_t, ITV8::RectangleF> TTrackMap;
    typedef TTrackMap::const_iterator TTrackMapIterator;

    enum EIgnoreMessage
    {
        eimSetMask = 0x01,
        eimUnsupportedMultimediaBuffer = 0x02,
        eimSetPolylineArray = 0x04,
        eimSetValue = 0x08,
        eimDefaultPropName = 0x10
    };

    ITV8::hresult_t SetRectangle(ITV8::uint32_t id, const ITV8::RectangleF& rectangle);

    EIgnoreMessage m_ignoreMessage;

    inline bool IsFirst(EIgnoreMessage type)
    {
        bool res = (m_ignoreMessage&type) == 0;
        m_ignoreMessage = (EIgnoreMessage) (m_ignoreMessage|type);
        return res;
    }
};

#endif //ITVSDKUTIL_CEVENTARGSADJUSTER_H

