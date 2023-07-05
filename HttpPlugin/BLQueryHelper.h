#pragma once

#include "GrpcReader.h"
#include "CommonUtility.h"
#include <Logging/log2.h>

#include <axxonsoft/bl/domain/Domain.grpc.pb.h>

namespace NWebBL
{
	using TEndpoints = std::vector<std::string>;
	using FAction = boost::function<void(const ::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >&, NWebGrpc::STREAM_ANSWER, grpc::Status) >;

	struct SAuxillarySources
	{
		std::string audioSource;
		std::string textSource;
	};

	using AuxillaryCallback_t = boost::function<void(SAuxillarySources)>;
	using EndpointCallback_t = boost::function<void(const std::string&, const std::string&)>;
	using StorageSourceCallback_t = boost::function<void(NPluginUtility::PEndpointQueryCollection_t)>;

	void QueryBLComponent(DECLARE_LOGGER_ARG, NWebGrpc::PGrpcManager grpcManager, NGrpcHelpers::PCredentials metaCredentials, const TEndpoints& endpoints, FAction action);

	void GetArchiveDepth(DECLARE_LOGGER_ARG, NWebGrpc::PGrpcManager grpcManager, NGrpcHelpers::PCredentials metaCredentials,
		const std::string& archive, const std::string& endpoint, std::function<void(bool, const boost::posix_time::ptime&)> cb);

	void ResolveCameraStorageSources(DECLARE_LOGGER_ARG, NWebGrpc::PGrpcManager grpcManager, NGrpcHelpers::PCredentials metaCredentials,
		const std::string& archiveName, const std::string& videoEp, const std::string& audioEp, EndpointCallback_t cb);

	void ResolveCameraAudioStream(DECLARE_LOGGER_ARG, NWebGrpc::PGrpcManager grpcManager, NGrpcHelpers::PCredentials metaCredentials,
		const std::string& videoEp, AuxillaryCallback_t ac);

	void ResolveStorageSources(DECLARE_LOGGER_ARG, NWebGrpc::PGrpcManager grpcManager, NGrpcHelpers::PCredentials metaCredentials,
                               NPluginUtility::PEndpointQueryCollection_t, StorageSourceCallback_t);
    }
