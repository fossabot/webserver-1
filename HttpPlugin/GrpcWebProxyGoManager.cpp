#include "GrpcWebProxyGoManager.h"
#include <InfraServer/CustomImR.h>

#include <boost/format.hpp>

namespace {
    const boost::posix_time::seconds TIMEOUT(10);

    class CGrpcWebProxyManager : public NPluginHelpers::IGrpcWebProxyManager,
                                 public std::enable_shared_from_this<CGrpcWebProxyManager>
    {
    public:
        CGrpcWebProxyManager(DECLARE_LOGGER_ARG, boost::asio::io_service& service, NCorbaHelpers::IContainer* c)
            : m_processName("APP_HOST.Grpcwebproxygo")
            , m_ioService(service)
            , m_customImr(InfraServer::CreateCustomImR(c->GetRootNC(), c->GetLogger(), false))
            , m_procTimer(service)
            
        {
            INIT_LOGGER_HOLDER;

            std::string workingDir;
            NCorbaHelpers::CEnvar::Lookup("NGP_WORK_DIR", workingDir);

            std::string cmd = (boost::format("grpcwebproxy --backend_addr=localhost:%1% --server_http_debug_port=%2% --allow_all_origins --backend_tls=true --backend_tls_noverify")
                               % NCorbaHelpers::CEnvar::NativeBLPort() % NCorbaHelpers::CEnvar::GrpcWebProxyPort()).str();

            _log_ << "Run Grpcwebproxy: " << cmd;

            m_customImr->Add(m_processName, cmd, workingDir, "", "");
        }

        void Start() override
        {
            _log_ << "CGrpcWebProxyManager::Start";

            std::weak_ptr<CGrpcWebProxyManager> wThis = shared_from_this();
            m_ioService.post([wThis]() 
                {
                    auto owner = wThis.lock();
                    if (owner)
                        owner->work();
                });
        }
        void Stop() override
        {
            stopImpl();
        }

        ~CGrpcWebProxyManager()
        {            
            stopImpl();
        }
    private:
        void stopImpl()
        {
            _log_ << "Enter CGrpcWebProxyManager::stopImpl";

            m_procTimer.cancel();

            if (m_customImr)
                m_customImr->Stop(m_processName);

            _log_ << "Exit CGrpcWebProxyManager::stopImpl";
        }
        void work()
        {
            run();
            timerHandler();
        }

        void run()
        {
            _log_ << "run GrpcWebProxyGo process";
            m_customImr->Start(m_processName);
        }

        void timerHandler()
        {
            m_procTimer.expires_from_now(TIMEOUT);
            std::weak_ptr<CGrpcWebProxyManager> wThis = shared_from_this();
            m_procTimer.async_wait([wThis](const boost::system::error_code& ec)
               {
                    if (ec)
                        return;

                    auto owner = wThis.lock();
                    if (owner)
                        owner->execTimerHandler();
               }
            );
        }

        void execTimerHandler()
        {
            if (!m_customImr || !m_customImr->IsStarted(m_processName))
            {
                _err_ << "miss GrpcWebProxyGo process";
                run();
            }

            timerHandler();
        }

    private:
        DECLARE_LOGGER_HOLDER;
        const std::string m_processName;

        boost::asio::io_service&   m_ioService;
        InfraServer::PCustomImR    m_customImr;

        boost::asio::deadline_timer   m_procTimer;
    };
}

namespace NPluginHelpers
{
    PGrpcWebProxyManager CreateGrpcWebManager(DECLARE_LOGGER_ARG , boost::asio::io_service& service, NCorbaHelpers::IContainer* c)
    {
        return PGrpcWebProxyManager(new CGrpcWebProxyManager(GET_LOGGER_PTR, service, c));
    }
}