#ifndef COMMON_UTILITY_H__
#define COMMON_UTILITY_H__

#include "GrpcReader.h"
#include "GrpcHelpers.h"

#include <HttpServer/HttpServer.h>

#include <ORM_IDL/ORMC.h>
#include <mmss/MMIDL/MMStorageS.h>

#include <axxonsoft/bl/archive/ArchiveSupport.grpc.pb.h>
#include <axxonsoft/bl/domain/Domain.grpc.pb.h>

namespace NCorbaHelpers
{
    class IContainer;
}

namespace NMMSS
{
    class ISample;
}

namespace NPluginUtility
{
    struct SEndpointQueryContext
    {
        std::string id;
        std::string endpoint;
        std::string archiveName;
        std::string requestedItem;
    };
    using PEndpointQueryContext = std::shared_ptr<SEndpointQueryContext>;
    using PEndpointQueryCollection_t = std::shared_ptr<std::vector<PEndpointQueryContext>>;

    struct SCameraQueryContext
    {
        PEndpointQueryContext videoCtx;
        PEndpointQueryContext audioCtx;
    };
    using PCameraQueryContext = std::shared_ptr<SCameraQueryContext>;

    void SendText(NHttp::PResponse&, const std::string&, bool resetResponse = false, bool headersOnly = false);
    void SendText(NHttp::PResponse&, NHttp::IResponse::EStatus statusCode, const std::string&, bool resetResponse = false, bool headersOnly = false);
    void SendText(const NHttp::PRequest, NHttp::PResponse, const std::string&, NHttp::IResponse::EStatus statusCode = NHttp::IResponse::OK, int maxAge = -1);

    void ParseHostname(const std::string& epName, std::string& hostName);
    std::string GetAudioEpFromVideoEp(const std::string &videoEp);

    void GetEndpointStorageSource(DECLARE_LOGGER_ARG, const::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >& cams,
        PEndpointQueryContext, bool includeEmbedded = true, bool activatedOnly = false);
    void GetCameraStorageSources(DECLARE_LOGGER_ARG, const::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >& cams, PCameraQueryContext, bool includeEmbedded = true);

    ORM::AsipDatabase_ptr GetDBReference(DECLARE_LOGGER_ARG, NCorbaHelpers::IContainer* cont, const std::string& hostName);

    std::string Convert(const ::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::DetectorEventInfo > &events);

    std::string convertToMainStream(const std::string& cam);

    template<typename T>
    bool eq(T a, T b)
    {
        return fabs(a - b) < std::numeric_limits<T>::epsilon();
    }

    NGrpcHelpers::PCredentials  GetCommonCredentials(DECLARE_LOGGER_ARG, const NHttp::IRequest::AuthSession& as);

    bool IsKeyFrame(NMMSS::ISample*);

    MMSS::StorageEndpoint_var ResolveEndoint(NCorbaHelpers::IContainer* cont, const axxonsoft::bl::media::EndpointRef& in);

    MMSS::StorageEndpoint_var ResolveEndoint(NWebGrpc::PGrpcManager grpcManager, NGrpcHelpers::PCredentials credentials, NCorbaHelpers::IContainer* cont,
        const std::string& accessPoint, const std::string& startTime, axxonsoft::bl::archive::EStartPosition pos, const long playFlags);

    MMSS::EStartPosition ToCORBAPosition(axxonsoft::bl::archive::EStartPosition pos);

    void SendGRPCError(NHttp::PResponse, grpc::Status);

    std::string parseForCC(std::uint32_t fourCC);
}

#endif // COMMON_UTILITY_H__
