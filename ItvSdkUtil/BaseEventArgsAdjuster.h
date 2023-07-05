#ifndef ITVSDKUTIL_BASEEVENTARGSADJUSTER_H
#define ITVSDKUTIL_BASEEVENTARGSADJUSTER_H

#include <string>

#include <boost/shared_ptr.hpp>

#include <ItvDetectorSdk/include/ItvDetectorsSdk2.h>

#include <Logging/log2.h>

namespace NMMSS
{
class IDetectorEvent;
}

class BaseEventArgsAdjuster : public ITV8::Analytics::IEventArgsAdjuster
{
private:
    // ITV8::IContract implementation
    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY(ITV8::IContract)
        ITV8_CONTRACT_ENTRY(ITV8::ISyncAdjuster)
        ITV8_CONTRACT_ENTRY(ITV8::Analytics::IVisualSyncAdjuster)
        ITV8_CONTRACT_ENTRY(ITV8::Analytics::IEventArgsAdjuster)
    ITV8_END_CONTRACT_MAP()

public:
    BaseEventArgsAdjuster(DECLARE_LOGGER_ARG, const std::string& prefix, NMMSS::IDetectorEvent* event);

public:
    // ITV8::Analytics::IEventArgsAdjuster implementation
    virtual ITV8::hresult_t SetValue(const char* name, ITV8::bool_t val); // final/sealed
    virtual ITV8::hresult_t SetValue(const char* name, ITV8::int32_t val); // final/sealed
    virtual ITV8::hresult_t SetValue(const char* name, ITV8::double_t val); // final/sealed
    virtual ITV8::hresult_t SetValue(const char* name, const char* val); // final/sealed

    virtual ITV8::hresult_t SetMultimediaBuffer(const char* name,
        ITV8::MFF::IMultimediaBuffer* multimediaBuffer); // final/sealed

    virtual ITV8::hresult_t SetTimestamp(ITV8::timestamp_t val);

    virtual ITV8::hresult_t SetRectangleArray(const char* name, ITV8::IRectangleEnumerator* collection); // final/sealed
    virtual ITV8::hresult_t SetMask(const char* name, ITV8::IMask* mask); // final/sealed
    virtual ITV8::hresult_t SetPolylineArray(const char* name, ITV8::IPolylineEnumerator* collection); // final/sealed

protected:
    virtual ITV8::hresult_t SetValue(const std::string& name, ITV8::bool_t val);
    virtual ITV8::hresult_t SetValue(const std::string& name, ITV8::int32_t val);
    virtual ITV8::hresult_t SetValue(const std::string& name, ITV8::double_t val);
    virtual ITV8::hresult_t SetValue(const std::string& name, const std::string& val);

    virtual ITV8::hresult_t SetMultimediaBuffer(const std::string& name,
        boost::shared_ptr<ITV8::MFF::IMultimediaBuffer> multimediaBuffer);

    virtual ITV8::hresult_t SetRectangleArray(const std::string& name, ITV8::IRectangleEnumerator* collection);
    virtual ITV8::hresult_t SetMask(const std::string& name, ITV8::IMask* mask);
    virtual ITV8::hresult_t SetPolylineArray(const std::string& name, ITV8::IPolylineEnumerator* collection);

    NMMSS::IDetectorEvent* event() const;
    const std::string& prefix() const;

    // Helper functions.
    virtual ITV8::hresult_t setRectangle(ITV8::uint32_t id, const ITV8::RectangleF& rectangle, NMMSS::IDetectorEvent* event = 0);
    ITV8::hresult_t setValue(const std::string& name, const std::string& val, NMMSS::IDetectorEvent* event = 0);

    template <typename TEnumerator, typename TFunctor>
    ITV8::hresult_t iterateEnumerator(TEnumerator enumerator, TFunctor func)
    {
        enumerator->Reset();
        while (enumerator->MoveNext())
        {
            // TODO: Need auto for obtaining current only once.
            if (0 == enumerator->GetCurrent())
            {
                _err_ << prefix() << " TEnumerator::GetCurrent() return 0." << std::endl;
                return ITV8::EInvalidParams;
            }

            const ITV8::hresult_t res = func(enumerator->GetCurrent());
            if (ITV8::ENotError != res)
            {
                return res;
            }
        }
        return ITV8::ENotError;
    }

protected:
    // Share logger with all inheritors.
    DECLARE_LOGGER_HOLDER;

private:
    NMMSS::IDetectorEvent* const m_event;
    const std::string m_prefix;

    enum EIgnoreMessage
    {
        eimSetValue =             1 << 0,
        eimSetMultimediaBuffer =  1 << 1,
        eimSetRectangleArray =    1 << 2,
        eimSetMask =              1 << 3,
        eimSetPolylineArray =     1 << 4,
        eimSetRelatedZonesArray = 1 << 5
    };
    int m_ignoreMessage;

    inline bool IsFirst(EIgnoreMessage type)
    {
        bool res = (m_ignoreMessage & type) == 0;
        m_ignoreMessage = m_ignoreMessage | type;
        return res;
    }
};

#endif // ITVSDKUTIL_BASEEVENTARGSADJUSTER_H
