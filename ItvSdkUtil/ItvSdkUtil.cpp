// ItvSdkUtiles.cpp : Defines the entry point for the DLL application.
//
#include "ItvSdkUtil.h"
#include "CFrameFactory.h"
#include "CLogger.h"
#include "CDetectorEventFactory.h"
#include "StatisticsSinkImpl.h"

#include <boost/thread/mutex.hpp>
#include <boost/smart_ptr/make_shared.hpp>
#include "../DeviceInfo/include/RepoLoader.h"

namespace ITVSDKUTILES
{

ITVSDKUTILES_API  ITargetEnumeratorFactoryPtr CreateTargetEnumeratorFactory(DECLARE_LOGGER_ARG, 
    NMMSS::IAllocator* allocator, const char* name)
{
    ITargetEnumeratorFactoryPtr factory(new CFrameFactory(GET_LOGGER_PTR, allocator, name));
    return factory;
}

ITVSDKUTILES_API  IMultimediaFrameFactoryPtr CreateFrameFactory(DECLARE_LOGGER_ARG, 
    NMMSS::IAllocator* allocator, const char* name)
{
    return CreateTargetEnumeratorFactory(GET_LOGGER_PTR, allocator, name);
}

ITVSDKUTILES_API  IEventFactoryPtr CreateEventFactory(DECLARE_LOGGER_ARG,
    NMMSS::PDetectorEventFactory factory, const char* endpointName, NStatisticsAggregator::IStatisticsAggregatorImpl* statAggregator)
{
    if (nullptr != endpointName && nullptr != statAggregator)
        return boost::make_shared<StatisticsSinkImpl<CDetectorEventFactory>>(GET_LOGGER_PTR, endpointName, statAggregator, std::move(factory));
    return boost::make_shared<CDetectorEventFactory>(GET_LOGGER_PTR, factory, endpointName);
}

ITVSDKUTILES_API  IDeviceNodeEventFactoryPtr CreateDeviceNodeEventFactory(DECLARE_LOGGER_ARG,
    NMMSS::PDetectorEventFactory factory, const char* prefix)
{
    IDeviceNodeEventFactoryPtr deviceNodeFactory(new CDeviceNodeEventFactory(GET_LOGGER_PTR, factory, prefix));
    return deviceNodeFactory;
}

namespace
{
    static std::unique_ptr<ITV8::GDRV::RepoLoader> theInstance;
    static boost::mutex instanceMutex;
}

class RepoLoaderSingleton
{
public:
    static ITV8::GDRV::RepoLoader* GetInstance(ITV8::ILogger* logger)
    {
        if (!theInstance)
        {
            boost::mutex::scoped_lock lock(instanceMutex);
            if (!theInstance)
                theInstance = std::make_unique<ITV8::GDRV::RepoLoader>(logger);
        }
        return theInstance.get();
    }
    static void RemoveInstance()
    {
        boost::mutex::scoped_lock lock(instanceMutex);
        theInstance.reset();
    }
};

ITVSDKUTILES_API ITV8::GDRV::RepoLoader* GetRepoLoader(ITV8::ILogger* logger)
{
    return RepoLoaderSingleton::GetInstance(logger);
}

ITVSDKUTILES_API int LoadRepository(ITV8::GDRV::RepoLoader* loader,
    const char* filename, ITV8::GDRV::DriverInfo& out)
{
    return loader->load(filename, out);
}

ITVSDKUTILES_API void ReleaseRepoLoader()
{
    RepoLoaderSingleton::RemoveInstance();
}

}
