#if !defined(ITVSDKUTIL_ITVSDKUTIL_H)
#define ITVSDKUTIL_ITVSDKUTIL_H

#include <boost/shared_ptr.hpp>

#include <ITV/ItvSdkWrapper.h>
#include <ItvSdk/include/ItvExport.h>
#include <ItvSdk/include/IErrorService.h>
#include <ItvMediaSdk/include/frameFactorySdk.h>
#include <ItvDetectorSdk/include/ItvDetectorsSdk2.h>

#include "../MMClient/DetectorEventFactory.h"

#include <Logging/log2.h>

#include "ISampleContainer.h"
#include "MediaFormatDictionary.h"
#include "CDetectorEventFactory.h"

#ifdef ITVSDKUTILES_EXPORTS
#define ITVSDKUTILES_API ITV8_EXPORT
#else
#define ITVSDKUTILES_API ITV8_IMPORT
#endif

namespace ITV8
{
    struct ILogger;
namespace GDRV
{
    class RepoLoader;
    class Version;
    struct DriverInfo;
}
}

namespace NStatisticsAggregator
{
    class IStatisticsAggregatorImpl;
}

namespace NMMSS
{
    //Predefinition of IAllocator 
    class IAllocator;
}

namespace ITVSDKUTILES
{
    typedef boost::shared_ptr<ITV8::MFF::IMultimediaFrameFactory> IMultimediaFrameFactoryPtr;
    typedef boost::shared_ptr<ITV8::Analytics::ITargetEnumeratorFactory> ITargetEnumeratorFactoryPtr;
    typedef boost::shared_ptr<ITV8::MFF::IBlobFrameFactory> IBlobFrameFactoryPtr;
    typedef boost::shared_ptr<ITV8::ILogger> ILoggerPtr;
    typedef boost::shared_ptr<ITimedEventFactory> IEventFactoryPtr;
    typedef boost::shared_ptr<IDeviceNodeEventFactory> IDeviceNodeEventFactoryPtr;

    // Creates factory for Multimedia Frames. Every frame created with the factory supports internal
    // interface ITVSDKUTILES::ISampleContainer which should be used to extract NMMSS::ISample from 
    // ITV8::MFF::IMultimediaBuffer.
    // name - Specifies the name of factory instance to distinguish one factory from the other in log.
    ITVSDKUTILES_API  IMultimediaFrameFactoryPtr CreateFrameFactory(DECLARE_LOGGER_ARG, 
        NMMSS::IAllocator* allocator, const char* name);

    // Creates factory for Multimedia Frames and Target Enumerators. Every frame created with the
    // name - Specifies the name of factory instance to distinguish one factory from the other in log.
    ITVSDKUTILES_API  ITargetEnumeratorFactoryPtr CreateTargetEnumeratorFactory(DECLARE_LOGGER_ARG, 
        NMMSS::IAllocator* allocator, const char* name);
    
    // Creates factory for blob frames.
    ITVSDKUTILES_API  IBlobFrameFactoryPtr CreateBlobFrameFactory(DECLARE_LOGGER_ARG);

    // Creates ITV8::ILogger instance which implements logging into ngp log files. 
    // name - Specifies the logger name for distinguishing one instance from other in log.
    ITVSDKUTILES_API  ILoggerPtr CreateLogger(DECLARE_LOGGER_ARG, const char* name=0);

    // Creates wrapper with ITV8::Analytics::IEventFactory interface around NMMSS::IDetectorEventFactory around 
    // which implements publishing events for NGP infrastructure. 
    // factory - The instance of NMMSS::IDetectorEventFactory interface. Use 
    //  NMMSS::CreateDetectorEventFactory to create NMMSS::IDetectorEventFactory instance.
    // resolver - callback interface allows to get detector identifier by pointer.
    // endpointName - Specifies the endpoint name for publishing statistics and distinguishing IEventFactory instance from other in log.
    ITVSDKUTILES_API  IEventFactoryPtr CreateEventFactory(DECLARE_LOGGER_ARG,
        NMMSS::PDetectorEventFactory factory, const char* endpointName = nullptr, NStatisticsAggregator::IStatisticsAggregatorImpl* statAggregator = nullptr);

    // Do same as CreateEventFactory, but for DeviceNode (Unit) objects
    ITVSDKUTILES_API  IDeviceNodeEventFactoryPtr CreateDeviceNodeEventFactory(DECLARE_LOGGER_ARG,
        NMMSS::PDetectorEventFactory factory, const char* prefix = 0);

    ITVSDKUTILES_API const IMediaFormatDictionary& GetMediaFormatDictionary(DECLARE_LOGGER_ARG);

    ITVSDKUTILES_API ITV8::GDRV::RepoLoader* GetRepoLoader(ITV8::ILogger* logger = 0);

    ITVSDKUTILES_API int LoadRepository(ITV8::GDRV::RepoLoader* loader,
        const char* filename, ITV8::GDRV::DriverInfo& out);

    ITVSDKUTILES_API void ReleaseRepoLoader();

}//ITVSDKUTILES

#endif //ITVSDKUTIL_ITVSDKUTIL_H
