#ifndef HTTP_PLUGIN_HLS_H__
#define HTTP_PLUGIN_HLS_H__

#include <vector>
#include <memory>
#include <functional>

#include <HttpServer/HttpServer.h>
#include <MMTransport/MMTransport.h>
#include "RegexUtility.h"
#include "VideoSourceCache.h"

namespace NPluginHelpers
{
class HlsSourceManagerImpl;

namespace npu = NPluginUtility;

typedef std::function<void(NMMSS::ISample*)>  OnSampleHandler;
typedef std::function<void()>                 OnDisconnected;

class HlsSourceManager
{
public:
    HlsSourceManager(NCorbaHelpers::IContainer* c, const std::string& hlsContentPath, NHttp::PVideoSourceCache cache);
    ~HlsSourceManager();

    void ConnectToEnpoint(NHttp::PRequest req, NHttp::PResponse resp, 
        const std::string& endpoint, const npu::TParams& params);

    void ConnectToArchiveEnpoint(const NWebGrpc::PGrpcManager grpcManager, NGrpcHelpers::PCredentials credentials, 
        NHttp::PRequest req, NHttp::PResponse resp, const std::string& endpoint,
        const std::string& archiveName, const std::string& startTime,
        const npu::TParams& params);

    bool HandleCommand(NHttp::PRequest req,
        const npu::TParams& params, NHttp::PResponse resp);

    void Erase(const std::string& connectionId);

private:
    std::unique_ptr<HlsSourceManagerImpl> m_impl;
};

}

#endif
