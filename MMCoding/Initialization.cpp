#include <memory>
#include <cstring>
#include <CorbaHelpers/LoadShlib.h>
#include <CorbaHelpers/RefcountedImpl.h>
#include <boost/thread/mutex.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/bind.hpp>
#include <boost/algorithm/string/case_conv.hpp>


#include "Transforms.h"
#include "Initialization.h"
#include "HooksPluggable.h"
#include "HWAccelerated.h"
#include "../NgpCodecManager/NgpCodecManager.h"


namespace
{
boost::mutex g_mutex;

std::string const ITV_CODEC_MANAGER_LIBRARY = NGP_MODULE_NAME(NgpCodecManager);

class CMMCodingInit: public NCorbaHelpers::CWeakReferableImpl
{
    DECLARE_LOGGER_HOLDER;
public:
    CMMCodingInit(DECLARE_LOGGER_ARG)
    {
        INIT_LOGGER_HOLDER;
        TRACE_BLOCK;
        InitItvCodecManager();
    }
    ~CMMCodingInit()
    {
        NMMSS::DeinitHardwareAccelerationForVideoDecoding();
        TRACE_BLOCK;
    }
    NMMSS::IFilter* CreateVideoDecoder_ITV_Codec_Manager(NMMSS::IFrameGeometryAdvisor* advisor)
    {
        if(!m_itvCodecManagerContext.get())
            return 0;
        return m_itvCodecManagerContext->CreateDecoderPullFilter(this, advisor);
    }

private:
    void InitItvCodecManager()
    {
        NCorbaHelpers::NShlib::TSymbols symbols;
        symbols.push_back("CreateContext");
        NCorbaHelpers::NShlib::TAddresses addresses;
        std::string errmsg;

        m_itvCodecManagerShlib=NCorbaHelpers::NShlib::LoadShlibGetSymbols(ITV_CODEC_MANAGER_LIBRARY.c_str(), 
            symbols, addresses, errmsg, false);
        if(!m_itvCodecManagerShlib.get())
        {
            _wrn_ << "CMMCodingInit::InitItvCodecManager(): could not load " << ITV_CODEC_MANAGER_LIBRARY << ": " << errmsg;
            return;
        }
        if(addresses.size()<1 || !addresses[0])
        {
            _wrn_ << "CMMCodingInit::InitItvCodecManager(): could not lookup required entry point in " << ITV_CODEC_MANAGER_LIBRARY;
            m_itvCodecManagerShlib.reset();
            return;
        }

        m_itvCodecManagerContext.reset(((NCodecManager::PFCreateContext)addresses[0])(GET_LOGGER_PTR), 
            boost::mem_fn(&NCodecManager::IContext::Destroy));
    }
    NCorbaHelpers::NShlib::TLibrary m_itvCodecManagerShlib;
    boost::shared_ptr<NCodecManager::IContext> m_itvCodecManagerContext;
};

NCorbaHelpers::CWeakPtr<CMMCodingInit> g_initWeakRef(0);

NCorbaHelpers::CAutoPtr<CMMCodingInit> DoMMCodingInitialize(DECLARE_LOGGER_ARG)
{
    boost::mutex::scoped_lock lock(g_mutex);
    NCorbaHelpers::CAutoPtr<CMMCodingInit> res(g_initWeakRef);
    if(!res)
    {
        try
        {
            res=new CMMCodingInit(GET_LOGGER_PTR);
            g_initWeakRef=NCorbaHelpers::CWeakPtr<CMMCodingInit>(res);
        }
        catch(const std::exception& e)
        {
            _log_ << "MMCodingInitialize(): exception: " << e.what();
            return NCorbaHelpers::CAutoPtr<CMMCodingInit>();
        }
    }
    return res;
}

}

namespace NMMSS
{

CMMCodingInitialization::CMMCodingInitialization(DECLARE_LOGGER_ARG)
:   m_initImpl(DoMMCodingInitialize(GET_LOGGER_PTR).Dup())
{
    if(!m_initImpl)
        throw std::runtime_error("MMCoding library cannot be initialized");
}
CMMCodingInitialization::~CMMCodingInitialization()
{
    m_initImpl->Release();
}

namespace
{
    enum ECodecPackUsagePolicy { ECPUP_Default=0, ECPUP_Disable, ECPUP_Prefer };
    ECodecPackUsagePolicy EvalCodecPackUsagePolicy()
    {
        const char* policyPtr = getenv("NGP_USE_ITV_CODEC_PACK");
        if(policyPtr)
        {
            std::string pol(policyPtr);
            boost::algorithm::to_lower(pol);

            if("default" == pol)
                return ECPUP_Default;
            if("disable" == pol)
                return ECPUP_Disable;
            if("prefer" == pol)
                return ECPUP_Prefer;
            return ECPUP_Default;
        }
        else
            return ECPUP_Default;
    }
    const ECodecPackUsagePolicy g_codecPackUsagePolicy=EvalCodecPackUsagePolicy();
}

namespace
{
NMMSS::IFilter* DoCreatePluggableDecoder(NMMSS::IFrameGeometryAdvisor* advisor) 
{
    boost::mutex::scoped_lock lock(g_mutex);
    NCorbaHelpers::CAutoPtr<CMMCodingInit> init(g_initWeakRef);
    if(!init)
        throw std::runtime_error("MMCoding library was not initialized");
    return init->CreateVideoDecoder_ITV_Codec_Manager(advisor);
}

NMMSS::IFilter* CreatePluggableVideoDecoderFilterT(EPluggableFilterPriority priority, 
                                                                     DECLARE_LOGGER_ARG, NMMSS::IFrameGeometryAdvisor* advisor) 
{
    if(ECPUP_Disable==g_codecPackUsagePolicy)
        return 0;
    if(PFP_AboveStandard==priority && ECPUP_Prefer==g_codecPackUsagePolicy)
    {
        _log_ << "ITVCodecManager has high priority in decoding process";
        return DoCreatePluggableDecoder(advisor);
    }
    if(PFP_BelowStandard==priority && ECPUP_Default==g_codecPackUsagePolicy)
        return DoCreatePluggableDecoder(advisor);
    return 0;
}

}

NMMSS::IFilter* CreatePluggableVideoDecoderPullFilter(EPluggableFilterPriority priority, 
                                                                    DECLARE_LOGGER_ARG, NMMSS::IFrameGeometryAdvisor* advisor)
{
    return CreatePluggableVideoDecoderFilterT(priority, GET_LOGGER_PTR, advisor);
}

namespace details
{
    NCorbaHelpers::IRefcounted* GetShareableMMCodingInitializationImpl(DECLARE_LOGGER_ARG)
    {
        return DoMMCodingInitialize(GET_LOGGER_PTR).Dup();
    }
}
}
