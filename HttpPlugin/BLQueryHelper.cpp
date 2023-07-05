#include "BLQueryHelper.h"

#include <boost/date_time/posix_time/posix_time.hpp>

namespace
{
    NPluginUtility::PEndpointQueryContext createArchiveContext(const std::string& archiveName, const std::string& ep)
    {
        NPluginUtility::PEndpointQueryContext videoCtx = std::make_shared<NPluginUtility::SEndpointQueryContext>();
        videoCtx->endpoint = ep;
        videoCtx->archiveName = archiveName;
        return videoCtx;
    }
}

namespace NWebBL
{
    namespace bl = axxonsoft::bl;

    using BatchCameraComponentReader_t = NWebGrpc::AsyncStreamReader < bl::domain::DomainService, bl::domain::GetCamerasByComponentsRequest,
        bl::domain::GetCamerasByComponentsResponse >;
    using PBatchCameraComponentReader_t = std::shared_ptr < BatchCameraComponentReader_t >;

    using IntervalReader_t = NWebGrpc::AsyncResultReader < bl::archive::ArchiveService, bl::archive::GetHistoryRequest,
        bl::archive::GetHistoryResponse >;
    using PIntervalReader_t = std::shared_ptr < IntervalReader_t >;

    void QueryBLComponent(DECLARE_LOGGER_ARG, NWebGrpc::PGrpcManager grpcManager, NGrpcHelpers::PCredentials metaCredentials, const TEndpoints& endpoints, NWebBL::FAction action)
    {
        bl::domain::GetCamerasByComponentsRequest creq;

        std::size_t epCount = endpoints.size();
        for (std::size_t i = 0; i < epCount; ++i)
        {
            bl::domain::ResourceLocator* rl = creq.add_items();
            rl->set_access_point(endpoints[i]);
        }

        PBatchCameraComponentReader_t grpcReader(new BatchCameraComponentReader_t
            (GET_LOGGER_PTR, grpcManager, metaCredentials, &bl::domain::DomainService::Stub::AsyncGetCamerasByComponents));

        auto ctxOut = std::make_shared<std::string>();

        grpcReader->asyncRequest(creq, [action](const bl::domain::GetCamerasByComponentsResponse& res, NWebGrpc::STREAM_ANSWER status, grpc::Status grpcStatus)
        {
            action(res.items(), status, grpcStatus);
        });
    }

    void GetArchiveDepth(DECLARE_LOGGER_ARG, NWebGrpc::PGrpcManager grpcManager, NGrpcHelpers::PCredentials metaCredentials,
        const std::string& archive, const std::string& endpoint, std::function<void(bool, const boost::posix_time::ptime&)> cb)
    {
        TEndpoints eps{ endpoint };
        NPluginUtility::PEndpointQueryContext ctx = createArchiveContext(archive, endpoint);

        auto depthCallback = [ngp_Logger_Ptr_, grpcManager, metaCredentials, cb](const std::string& arcAccessPont)
        {
            if (arcAccessPont.empty())
                return cb(false, boost::posix_time::ptime());

            PIntervalReader_t reader(new IntervalReader_t
                (GET_LOGGER_PTR, grpcManager, metaCredentials, &bl::archive::ArchiveService::Stub::AsyncGetHistory));


            bl::archive::GetHistoryRequest creq;
            creq.set_access_point(arcAccessPont);

            auto t1 = boost::posix_time::to_iso_string(boost::posix_time::ptime(boost::date_time::min_date_time));
            auto t2 = boost::posix_time::to_iso_string(boost::posix_time::ptime(boost::date_time::max_date_time));

            creq.set_begin_time(t1);
            creq.set_end_time(t2);
            creq.set_max_count(1u);
            creq.set_min_gap(1000u);

            reader->asyncRequest(creq, [cb](const bl::archive::GetHistoryResponse& res, grpc::Status status)
                {
                    static const boost::posix_time::ptime zeroTime(boost::posix_time::time_from_string("1900-01-01 00:00:00.000"));

                    if (!status.ok() || res.intervals_size() < 1)
                        return NCorbaHelpers::GetReactorInstanceShared()->GetIO().post(boost::bind(cb, false, boost::posix_time::ptime()));

                    const bl::archive::GetHistoryResponse::Interval& interval = res.intervals(0);
                    NCorbaHelpers::GetReactorInstanceShared()->GetIO().post(boost::bind(cb, true, zeroTime + boost::posix_time::milliseconds(interval.begin_time())));
                }
            );
        };

        FAction action = [ngp_Logger_Ptr_, ctx, depthCallback](const ::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >& items,
            NWebGrpc::STREAM_ANSWER status, grpc::Status grpcStatus)
        {
            if (!grpcStatus.ok())
            {
                depthCallback("");
                return;
            }

            NPluginUtility::GetEndpointStorageSource(GET_LOGGER_PTR, items, ctx, false);

            if (status == NWebGrpc::_FINISH)
                depthCallback(ctx->requestedItem);
        };
        QueryBLComponent(GET_LOGGER_PTR, grpcManager, metaCredentials, eps, action);
    }

    void ResolveCameraStorageSources(DECLARE_LOGGER_ARG, NWebGrpc::PGrpcManager grpcManager, NGrpcHelpers::PCredentials metaCredentials,
        const std::string& archiveName, const std::string& videoEp, const std::string& audioEp, EndpointCallback_t cb)
    {
        TEndpoints eps{ videoEp };
        NPluginUtility::PCameraQueryContext ctx = std::make_shared<NPluginUtility::SCameraQueryContext>();
        
        ctx->videoCtx = createArchiveContext(archiveName, videoEp);
        ctx->audioCtx = createArchiveContext(archiveName, audioEp);

        FAction action = [ngp_Logger_Ptr_, ctx, cb](const ::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >& items,
            NWebGrpc::STREAM_ANSWER status, grpc::Status grpcStatus)
        {
            if (!grpcStatus.ok())
            {
                NCorbaHelpers::GetReactorInstanceShared()->GetIO().post(boost::bind(cb, "", ""));
            }

            NPluginUtility::GetCameraStorageSources(GET_LOGGER_PTR, items, ctx);

            if (status == NWebGrpc::_FINISH)
            {
                NCorbaHelpers::GetReactorInstanceShared()->GetIO().post(boost::bind(cb, ctx->videoCtx->requestedItem, ctx->audioCtx->requestedItem));
            }
        };
        QueryBLComponent(GET_LOGGER_PTR, grpcManager, metaCredentials, eps, action);
    }

    void ResolveCameraAudioStream(DECLARE_LOGGER_ARG, NWebGrpc::PGrpcManager grpcManager, NGrpcHelpers::PCredentials metaCredentials,
        const std::string& videoEp, AuxillaryCallback_t ac)
    {
        TEndpoints eps{ videoEp };
        NPluginUtility::PEndpointQueryContext ctx = createArchiveContext("", videoEp);

        FAction action = [ctx, ac, auSources = SAuxillarySources{}](const ::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >& items,
            NWebGrpc::STREAM_ANSWER status, grpc::Status grpcStatus) mutable
        {
            if (!grpcStatus.ok())
            {
                NCorbaHelpers::GetReactorInstanceShared()->GetIO().post(boost::bind(ac, auSources));
                return;
            }

            if (items.size() > 0)
            {
                const bl::domain::Camera& c = items.Get(0);
                if (c.microphones_size() > 0 && c.microphones(0).is_activated())
                {
                    auSources.audioSource.assign(c.microphones(0).access_point());
                }

                if (c.text_sources_size() > 0)
                {
                    auSources.textSource.assign(c.text_sources(0).access_point());
                }
            }

            if (status == NWebGrpc::_FINISH)
            {
                NCorbaHelpers::GetReactorInstanceShared()->GetIO().post(boost::bind(ac, auSources));
            }
        };

        QueryBLComponent(GET_LOGGER_PTR, grpcManager, metaCredentials, eps, action);
    }

    void ResolveStorageSources(DECLARE_LOGGER_ARG,
                               NWebGrpc::PGrpcManager grpcManager,
                               NGrpcHelpers::PCredentials metaCredentials,
                               NPluginUtility::PEndpointQueryCollection_t epColl,
                               StorageSourceCallback_t cb)
    {
        TEndpoints eps;
        std::size_t epCount = epColl->size();
        for (std::size_t i = 0; i < epCount; ++i)
        {
            NPluginUtility::PEndpointQueryContext ctx = (*epColl)[i];
            eps.push_back(ctx->endpoint);

            _log_ << "Requested info for archive " << ctx->archiveName << " attached to endpoint " << ctx->endpoint;
        }

        FAction action = [ngp_Logger_Ptr_, epColl, cb](const ::google::protobuf::RepeatedPtrField<::axxonsoft::bl::domain::Camera>& items,
                                                    NWebGrpc::STREAM_ANSWER status,
                                                    grpc::Status grpcStatus) 
        {
            if (!grpcStatus.ok())
            {
                NCorbaHelpers::GetReactorInstanceShared()->GetIO().post(boost::bind(cb, epColl));
            }

            for (auto ctx : *epColl)
            {
                _log_ << "Resolve info for archive " << ctx->archiveName << " attached to endpoint " << ctx->endpoint;
                NPluginUtility::GetEndpointStorageSource(GET_LOGGER_PTR, items, ctx);
            }

            if (status == NWebGrpc::_FINISH)
            {
                NCorbaHelpers::GetReactorInstanceShared()->GetIO().post(boost::bind(cb, epColl));
            }
        };
        QueryBLComponent(GET_LOGGER_PTR, grpcManager, metaCredentials, eps, action);  
    }
}
