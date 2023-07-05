#ifndef DATA_SINK_H__
#define DATA_SINK_H__

#include <boost/shared_ptr.hpp>
#include <boost/optional/optional.hpp>
#include <boost/function.hpp>
#include <boost/thread/mutex.hpp>
#include <HttpServer/HttpResponse.h>
#include "MMSS.h"
#include "Sample.h"

namespace NPluginHelpers
{
    typedef boost::function1<void, NMMSS::ISample*>         TOnSampleHandler;
    typedef boost::function2<void, boost::posix_time::ptime, NMMSS::ISample*> TOnTimedSampleHandler;
    typedef boost::function1<void, bool /*containsMore*/>   TOnCompleteHandler;
    typedef boost::function0<void>                          TOnDisconnected;
    typedef std::function<void(NMMSS::PSample, NMMSS::PSample)> TContHandler;
    typedef std::function<void()>                           TErrorHandler;
    typedef std::function<void(NMMSS::PSample)>             TPushHandler;

    NMMSS::IPullStyleSink* CreateDummySink(DECLARE_LOGGER_ARG);
    NMMSS::IPullStyleSink* CreateOneShotSink(DECLARE_LOGGER_ARG, const TOnSampleHandler&);
    NMMSS::IPullStyleSink* CreateSink(const TOnSampleHandler &);
    NMMSS::IPullStyleSink* CreateAudioSink(DECLARE_LOGGER_ARG, const TOnSampleHandler &);

    NMMSS::IPullStyleSink* CreateLimitedSink(
        const TOnSampleHandler &,
        const TOnCompleteHandler &,
        const boost::posix_time::ptime &begin,
        const boost::posix_time::ptime &end,
        size_t samplesCount);

    NMMSS::IPullStyleSink* CreateFlowControlSink(NHttp::PResponse, const TOnDisconnected&, const TOnSampleHandler&);

    void SetSinkCounterHandler(boost::function1<void, int> handler);

    struct IFeedbackSink : public virtual NMMSS::IPullStyleSink
        , public virtual NCorbaHelpers::IWeakReferable
    {
        virtual void EnoughData() = 0;
        virtual void NeedMoreData() = 0;
    };

    typedef NCorbaHelpers::CAutoPtr<IFeedbackSink> PFeedbackSink;
    typedef NCorbaHelpers::CWeakPtr<IFeedbackSink> WFeedbackSink;

    IFeedbackSink* CreateThresholdSink(DECLARE_LOGGER_ARG, const TOnDisconnected&, const TOnSampleHandler &);

    struct IPullStyleSinkWithRequest : public virtual NMMSS::IPullStyleSink
    {
        virtual void Request(unsigned int) = 0;
    };

    struct IRequestSink : public virtual NMMSS::IPullStyleSink
    {
        virtual void SetRequester(IPullStyleSinkWithRequest*) = 0;
        virtual void SetContCallback(TContHandler, TErrorHandler, IRequestSink*) = 0;
        virtual void SetInitCallback(TPushHandler, TErrorHandler) = 0;
        virtual void SetPushCallback(TPushHandler) = 0;

        virtual void Stop() = 0;
    };
    typedef NCorbaHelpers::CAutoPtr<IRequestSink> PRequestSink;

    struct IRTSPRequestSink : public virtual NMMSS::IPullStyleSink
    {
        virtual NMMSS::PSample GetSample() = 0;
        virtual NMMSS::PSample PeekSample() = 0;

        virtual void SetRequester(IPullStyleSinkWithRequest*) = 0;
        virtual void ClearBuffer() = 0;

        virtual void Stop() = 0;
    };
    typedef NCorbaHelpers::CAutoPtr<IRTSPRequestSink> PRTSPRequestSink;

    IRequestSink* CreateGstreamerSink(DECLARE_LOGGER_ARG, const TOnDisconnected&);
    IRTSPRequestSink* CreateRTSPGstreamerSink(DECLARE_LOGGER_ARG, const TOnDisconnected&, std::uint32_t);

    NMMSS::IPullStyleSink* CreateWrapperSink(NMMSS::IPullStyleSink*, const TOnDisconnected = [](){});

    struct IBatchSink : public virtual NMMSS::IPullStyleSink
    {
        virtual void SetFragmentId(std::uint32_t, const boost::posix_time::ptime&) = 0;
    };
    typedef NCorbaHelpers::CAutoPtr<IBatchSink> PBatchSink;

    struct IReusableSink : public virtual NMMSS::IPullStyleSink
    {
        virtual void WakeUp(const boost::posix_time::ptime&) = 0;
    };
    typedef NCorbaHelpers::CAutoPtr<IReusableSink> PReusableSink;

    IBatchSink* CreateBatchSink(DECLARE_LOGGER_ARG, const std::string& streamId, int width, int height, bool crop, float crop_x, float crop_y, float crop_width, float crop_height, TOnTimedSampleHandler, TOnCompleteHandler);
    IReusableSink* CreateReusableSink(DECLARE_LOGGER_ARG, TOnTimedSampleHandler, TOnCompleteHandler);

    NMMSS::IPullStyleSink* CreateJPEGSink(DECLARE_LOGGER_ARG, int width, int height, IRequestSink*);

    NMMSS::IPullStyleSink* CreateTimeLimitedSink(DECLARE_LOGGER_ARG, NHttp::PResponse, const TOnDisconnected&, const std::string& /*stopTime*/);
}

#endif // DATA_SINK_H__
