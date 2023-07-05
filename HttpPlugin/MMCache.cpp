#include <map>
#include <mutex>

#include "MMCache.h"
#include <MMTransport/MMTransport.h>
#include <MMTransport/QualityOfService.h>

namespace
{
    struct SMMOrigin : public NHttp::IMMOrigin
    {
        DECLARE_LOGGER_HOLDER;
        NMMSS::PSinkEndpoint endpoint;
        NMMSS::PDistributor distributor;

        SMMOrigin(DECLARE_LOGGER_ARG, NCorbaHelpers::IContainer* cont, const std::string& sourceAddress, bool keyFrames)
            : distributor(NMMSS::CreateDistributor(GET_LOGGER_PTR, NMMSS::NAugment::UnbufferedDistributor{}))
        {
            INIT_LOGGER_HOLDER;
            auto qos = NMMSS::MakeQualityOfService(
                MMSS::QoSRequest::StartFrom{ MMSS::QoSRequest::StartFrom::Preroll }
            );
            if (keyFrames)
                NMMSS::SetRequest(qos, MMSS::QoSRequest::OnlyKeyFrames{ true });

            endpoint = NMMSS::CreatePullConnectionByNsref(GET_LOGGER_PTR,
                sourceAddress.c_str(), cont->GetRootNC(), distributor->GetSink(),
                MMSS::EAUTO, &qos);
        }
        ~SMMOrigin()
        {
            _log_ << "SMMOrigin dtor";
            endpoint->Destroy();
        }

        NMMSS::IPullStyleSource* GetSource() override
        {
            return distributor->CreateSource();
        }
    };
    using PMMOrigin = std::shared_ptr<SMMOrigin>;
    using WPMMOrigin = std::weak_ptr<SMMOrigin>;

    struct RequestParams
    {
        const std::string address;
        const bool keyFrames;

        bool operator <(const RequestParams& rhs) const
        {
            if (address < rhs.address)
                return true;
            else if (address == rhs.address)
                return keyFrames < rhs.keyFrames;

            return false;
        }
        RequestParams(const std::string& a, bool kf)
            : address(a)
            , keyFrames(kf)
        {}
    };
    using TMMOrigins = std::map<RequestParams, WPMMOrigin>;

	class CMMCache : public NHttp::IMMCache
	{
        DECLARE_LOGGER_HOLDER;
	public:
        CMMCache(NCorbaHelpers::IContainer* c)
            : m_container(c, NCorbaHelpers::ShareOwnership())
        {
            INIT_LOGGER_HOLDER_FROM_CONTAINER(c);
        }

        ~CMMCache()
        {
            _log_ << "MMCache dtor";
        }

		NHttp::PMMOrigin GetMMOrigin(const char* const access_point, bool keyFrames) override
		{
            return lookupOrigin(RequestParams(access_point, keyFrames));
        }

    private:
        PMMOrigin lookupOrigin(const RequestParams& r)
        {
            PMMOrigin res;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                TMMOrigins::iterator it(m_origins.find(r));
                if (m_origins.end() != it)
                {                    
                    res = it->second.lock();
                }
                if (res.get())
                {
                    _log_ << "Use cached origin";
                    return res;
                }

                _log_ << "Cache origin";
                res.reset(new SMMOrigin(GET_LOGGER_PTR, m_container.Get(), r.address, r.keyFrames));
                m_origins[r] = res;
            }
            return res;
        }

        NCorbaHelpers::PContainer m_container;

        std::mutex m_mutex;
        TMMOrigins m_origins;
	};
}

namespace NHttp
{
	PMMCache CreateMMCache(NCorbaHelpers::IContainer* c)
	{
		return PMMCache(new CMMCache(c));
	}
}
