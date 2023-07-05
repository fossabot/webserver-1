#include <CorbaHelpers/RefcountedImpl.h>
#include "../MMSS.h"
#include "../ConnectionBroker.h"
#include "HooksPluggable.h"
#include "Transforms.h"

namespace
{
class CDecoderFilterContent
{
public:
    NMMSS::IFilter* CreateDecoder(DECLARE_LOGGER_ARG, NMMSS::IFrameGeometryAdvisor* advisor, bool multithreaded)
    {
        return CreateStandardVideoDecoderPullFilter(GET_LOGGER_PTR, advisor, multithreaded);
    }
    NMMSS::IFilter* CreatePluggableDecoder(NMMSS::EPluggableFilterPriority priority, 
        DECLARE_LOGGER_ARG, NMMSS::IFrameGeometryAdvisor* advisor)
    {
        return CreatePluggableVideoDecoderPullFilter(priority, GET_LOGGER_PTR, advisor);
    }
protected:
    NMMSS::PPullFilter m_standard;
    NMMSS::PPullFilter m_override;
    NMMSS::PPullFilter m_fallback;
};

class CCompositeVideoDecoderFilter : 
    public virtual NMMSS::IFilter,
    public CDecoderFilterContent,
    public virtual NCorbaHelpers::CRefcountedImpl
{
    typedef NMMSS::IFilter TBase;
public:
    CCompositeVideoDecoderFilter(DECLARE_LOGGER_ARG, NMMSS::IFrameGeometryAdvisor* advisor, bool multithreaded)
        :   m_connectionOverride(0)
        ,   m_connectionFallback(0)
    {
        this->m_standard=this->CreateDecoder(GET_LOGGER_PTR, advisor, multithreaded);
        if(!this->m_standard)
            throw std::runtime_error("CCompositeVideoDecoderFilter ctor: standard video decoder creation failed");

        this->m_override=this->CreatePluggableDecoder(NMMSS::PFP_AboveStandard, GET_LOGGER_PTR, advisor);
        this->m_fallback=this->CreatePluggableDecoder(NMMSS::PFP_BelowStandard, GET_LOGGER_PTR, advisor);
        if(this->m_override)
            m_connectionOverride=NMMSS::GetConnectionBroker()->SetConnection(this->m_override->GetSource(), this->m_standard->GetSink(), GET_LOGGER_PTR);
        if(this->m_fallback)
            m_connectionFallback=NMMSS::GetConnectionBroker()->SetConnection(this->m_standard->GetSource(), this->m_fallback->GetSink(), GET_LOGGER_PTR);
    }
    ~CCompositeVideoDecoderFilter()
    {
        NMMSS::GetConnectionBroker()->DestroyConnection(m_connectionOverride);
        NMMSS::GetConnectionBroker()->DestroyConnection(m_connectionFallback);
    }
    TBase::TSink* GetSink()
    {
        if(this->m_override)
            return this->m_override->GetSink();
        else
            return this->m_standard->GetSink();
    }
    TBase::TSource* GetSource()
    {
        if(this->m_fallback)
            return this->m_fallback->GetSource();
        else
            return this->m_standard->GetSource();
    }
    virtual void AddUnfilteredFrameType(uint32_t major, uint32_t minor)
    {
        if (this->m_standard)
            this->m_standard->AddUnfilteredFrameType(major, minor);
        if (this->m_override)
            this->m_override->AddUnfilteredFrameType(major, minor);
        if (this->m_fallback)
            this->m_fallback->AddUnfilteredFrameType(major, minor);
    }
    virtual void RemoveUnfilteredFrameType(uint32_t major, uint32_t minor)
    {
        if (this->m_standard)
            this->m_standard->RemoveUnfilteredFrameType(major, minor);
        if (this->m_override)
            this->m_override->RemoveUnfilteredFrameType(major, minor);
        if (this->m_fallback)
            this->m_fallback->RemoveUnfilteredFrameType(major, minor);
    }
private:
    NMMSS::IConnectionBase* m_connectionOverride;
    NMMSS::IConnectionBase* m_connectionFallback;
};
}

namespace NMMSS
{

IFilter* CreateVideoDecoderPullFilter(DECLARE_LOGGER_ARG, IFrameGeometryAdvisor* advisor, bool multithreaded)
{
    return new CCompositeVideoDecoderFilter(GET_LOGGER_PTR, advisor, multithreaded);
}

}

