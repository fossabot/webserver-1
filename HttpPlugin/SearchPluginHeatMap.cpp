#include "HttpPlugin.h"
#include "SearchPlugin.h"
#include "GrpcReader.h"
#include "Constants.h"
#include "SendContext.h"

#include <CorbaHelpers/Reactor.h>
#include <HeatMap/HeatMapC.h>
#include <json/json.h>

#include <boost/asio.hpp>
#include <boost/date_time/posix_time/time_formatters.hpp>

#include <axxonsoft/bl/heatmap/HeatMap.grpc.pb.h>
#include "HeatMap/HeatMap.h"

using namespace NHttp;
using namespace NPluginUtility;
namespace bl = axxonsoft::bl;

namespace
{
    const std::uint32_t MAX_ACTIVE_HEAT_MAP_SEARCH_COUNT = 1000;

    const char* const AVDETECTOR_MASK = "AVDetector";
    const char* const OFFLINE_ANALYTICS_MASK = "OfflineAnalytics";

    const char* const QUERY_PARAMETER = "query";

    const char* const MASK_SIZE_PARAMETER = "mask_size";
    const char* const WIDTH_PARAMETER = "width";
    const char* const HEIGHT_PARAMETER = "height";

    using BuildHeatmap_t = NWebGrpc::AsyncResultReader < bl::heatmap::HeatMapService, bl::heatmap::BuildHeatmapRequest,
        bl::heatmap::BuildHeatmapResponse>;

    struct HeatMapSearchContext : public ISearchContext
    {
        DECLARE_LOGGER_HOLDER;
        HeatMapSearchContext(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager, NGrpcHelpers::PCredentials metaCredentials,
            std::string detectorId, std::string query, const NMMSS::HeatMapSize& mapSize, std::string beginTime, std::string endTime)
            : m_reactor(NCorbaHelpers::GetReactorInstanceShared())
            , m_grpcManager(grpcManager)
            , m_metaCredentials(metaCredentials)
            , m_detectorId(detectorId)
            , m_query(query)
            , m_mapSize(mapSize)
            , m_beginTime(beginTime)
            , m_endTime(endTime)
            , m_errorOccurred(false)
            , m_searchStopped(false)
            , m_searchFinished(false)
        {
            INIT_LOGGER_HOLDER;
        }

        ~HeatMapSearchContext()
        {
            _dbg_ << "HeatMap search context destroyed";
        }

        void StartSearch()
        {
            _dbg_ << "Starting HeatMap search...";
            if (m_reactor)
            {
                m_reactor->GetIO().post(std::bind(&HeatMapSearchContext::SearchTask, shared_from_base<HeatMapSearchContext>()));
            }
        }

        void StopSearch()
        {
            _dbg_ << "Stopping HeatMap search...";
            m_searchStopped = true;
        }

        void GetResult(const PRequest req, PResponse resp, size_t offset, size_t limit) const
        {
            if (m_errorOccurred)
            {
                Error(resp, IResponse::ServiceUnavailable);
                return;
            }

            std::string result;

            {
                std::unique_lock<std::mutex> lock(m_resultMutex);
                result = m_result;
            }

            resp->SetStatus(m_searchFinished ? NHttp::IResponse::OK : NHttp::IResponse::PartialContent);

            const std::size_t textSize = result.size();
            std::string contentType(NHttp::GetMIMETypeByExt("png"));
            resp << ContentLength(textSize)
                << ContentType(contentType)
                << CacheControlNoCache();

            resp->FlushHeaders();

            NContext::PSendContext ctx(NContext::CreateStringContext(resp, result));
            ctx->ScheduleWrite();
        }
    private:
        void SearchTask()
        {
            if (!m_searchStopped)
            {
                try
                {
                    _dbg_ << "Start building heatmap with query='" << m_query << "'.";

                    auto reader = std::make_shared<BuildHeatmap_t>(GET_LOGGER_PTR, m_grpcManager, m_metaCredentials,
                        &bl::heatmap::HeatMapService::Stub::AsyncBuildHeatmap);

                    invokeRequest(reader);
                    return;
                }
                catch (const std::exception& e)
                {
                    m_errorOccurred = true;
                    _err_ << "HeatMap search error: " << e.what();
                }
            }
            
            if (this->m_done)
                this->m_done();
        }

        template <typename TReader>
        void invokeRequest(TReader reader)
        {
            typename TReader::element_type::Request_t creq;
            creq.set_access_point("HeatMapBuilder.0/HeatMapBuilder");
            creq.set_camera_id(m_detectorId);
            creq.set_dt_posix_start_time(m_beginTime);
            creq.set_dt_posix_end_time(m_endTime);
            creq.set_result_type(bl::heatmap::RESULT_TYPE_IMAGE);
            creq.set_query(m_query);
            creq.mutable_mask_size()->set_width(m_mapSize.width);
            creq.mutable_mask_size()->set_height(m_mapSize.height);

            auto obj = std::weak_ptr<HeatMapSearchContext>(shared_from_base<HeatMapSearchContext>());

            reader->asyncRequest(creq, [obj](const typename TReader::element_type::Responce_t& res, grpc::Status status)
            {
                std::shared_ptr<HeatMapSearchContext> owner = obj.lock();
                if (owner)
                {
                    owner->processResult(res, status);
                }
            });
        }

        template <typename TResponce>
        void processResult(const TResponce& res, grpc::Status status)
        {
            _dbg_ << "Building heatmap finished. result=" << status.ok() << ".";

            {
                std::unique_lock<std::mutex> lock(m_resultMutex);
                m_result = res.image_data();
            }

            m_searchFinished = true;

            if (this->m_done)
                this->m_done();
        }

        NCorbaHelpers::PReactor m_reactor;

        const NWebGrpc::PGrpcManager m_grpcManager;
        NGrpcHelpers::PCredentials m_metaCredentials;
        const std::string m_detectorId;
        const std::string m_query;
        NMMSS::HeatMapSize m_mapSize;
        const std::string m_beginTime;
        const std::string m_endTime;

        mutable std::mutex m_resultMutex;
        std::string m_result;

        std::atomic<bool> m_errorOccurred;
        std::atomic<bool> m_searchStopped;
        std::atomic<bool> m_searchFinished;
    };

    class CHeatMapSearchContentImpl : public CSearchPlugin<heatMap::HeatMapBuilder>
    {

    public:
        CHeatMapSearchContentImpl(NCorbaHelpers::IContainer *c, const NWebGrpc::PGrpcManager grpcManager)
            : CSearchPlugin(c)
            , m_grpcManager(grpcManager)
        {
            this->m_maxActiveCount = MAX_ACTIVE_HEAT_MAP_SEARCH_COUNT;
        }

        PSearchContext CreateSearchContext(const NHttp::PRequest req, DB_var db, const std::string&, const std::vector<std::string>& origins,
            Json::Value& data, boost::posix_time::ptime beginTime, boost::posix_time::ptime endTime, bool descending) override
        {
            if (origins.size() != 1)
            {
                _err_ << "HeatMap search requires one object reference only";
                return PSearchContext();
            }

            std::string heatMapEntryPoint(GetHeatMapEntryPoint(origins[0]));
            if (heatMapEntryPoint.empty())
            {
                _err_ << "HeatMap entry point " << origins[0] << " formatted incorrectly";
                return PSearchContext();
            }

            if (data.isNull())
            {
                std::vector<uint8_t> body(req->GetBody());
                std::string bodyContent(reinterpret_cast<const char*>(&body[0]), body.size());

                Json::CharReaderBuilder reader;
                std::string err;
                std::istringstream is(bodyContent);
                if (!bodyContent.empty() && !Json::parseFromStream(reader, is, &data, &err))
                {
                    _err_ << "Error occured ( " << err << " ) during parsing body content: " << bodyContent;
                    return PSearchContext();
                }
            }

            try
            {
                const IRequest::AuthSession& as = req->GetAuthSession();
                NGrpcHelpers::PCredentials metaCredentials = NPluginUtility::GetCommonCredentials(GET_LOGGER_PTR, as);

                std::string query(ParseParameter(data, QUERY_PARAMETER, std::string()));
                NMMSS::HeatMapSize mapSize = {0, 0};

                ParseMaskSize(data, mapSize);

                _dbg_ << "HeatMap query: " << query;

                return PSearchContext(new HeatMapSearchContext(GET_LOGGER_PTR, m_grpcManager, metaCredentials,
                    heatMapEntryPoint, std::move(query), mapSize,
                    boost::posix_time::to_iso_string(beginTime),
                    boost::posix_time::to_iso_string(endTime)));
            }
            catch (const std::exception& e)
            {
                _err_ << e.what();
            }
            return PSearchContext();
        }

        void ParseMaskSize(const Json::Value& data, NMMSS::HeatMapSize& mapSize)
        {
            if (!data.isMember(MASK_SIZE_PARAMETER))
                return;
            Json::Value sizeValue = data[MASK_SIZE_PARAMETER];

            if (!sizeValue.isMember(WIDTH_PARAMETER) || !sizeValue.isMember(HEIGHT_PARAMETER))
                return;
            mapSize.width = ParseParameter(sizeValue, WIDTH_PARAMETER, mapSize.width);
            mapSize.height = ParseParameter(sizeValue, HEIGHT_PARAMETER, mapSize.height);
        }

        std::string GetHeatMapEntryPoint(const std::string& entryPoint)
        {
            size_t pos = entryPoint.find(OFFLINE_ANALYTICS_MASK);
            if (std::string::npos == pos)
            {
                pos = entryPoint.find(AVDETECTOR_MASK);
            }
            return entryPoint.substr(pos);
        }

        template <typename TRes, typename TParam>
        TRes ParseParameter(const Json::Value& data, TParam param, TRes defaultValue)
        {
            if (!data.isMember(param))
                return defaultValue;
            return getValue<TRes>(data[param]);
        }

        template <typename T>
        T getValue(const Json::Value& data);
    private:
        const NWebGrpc::PGrpcManager m_grpcManager;
    };

    template <typename T>
    T CHeatMapSearchContentImpl::getValue(const Json::Value& data)
    {
        throw std::invalid_argument("Unsupported type");
    }

    template <>
    std::string CHeatMapSearchContentImpl::getValue<std::string>(const Json::Value& data)
    {
        return data.asString();
    }

    template <>
    unsigned int CHeatMapSearchContentImpl::getValue<unsigned int>(const Json::Value& data)
    {
        return data.asUInt();
    }
}

namespace NHttp
{
    IServlet* CreateHeatMapSearchServlet(NCorbaHelpers::IContainer* c, const NWebGrpc::PGrpcManager grpcManager)
    {
        return new CHeatMapSearchContentImpl(c, grpcManager);
    }
}
