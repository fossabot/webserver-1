#pragma once

#include <cstdlib>
#include <functional>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <memory>

#include <Logging/Fwd.h>
#include <CorbaHelpers/WithReactor.h>
#include <Primitives/Executors/DynamicThreadPool.h>
#include <Primitives/CorbaHelpers/OrbMainTestHelper.h>
#include <ItvDeviceSdk/include/IStorageDevice.h>

#include "../IIPManager3.h"
#include "../StorageDataTypes.h"


namespace DeviceIpint_3 { namespace UnitTesting {

       

class BasicFixture:
    public NLogging::WithLogger,
    public NCorbaHelpers::UnitTesting::WithReactor
{
protected:
    NExecutors::PDynamicThreadPool m_ioPool;
    std::unique_ptr<IOrbMain> m_orbMain;
    NCorbaHelpers::IContainer* m_rootContainer;
    NCorbaHelpers::PContainerNamed m_contNamed;

public:
    BasicFixture();
    ~BasicFixture();

    boost::asio::io_context& GetIO();

    NExecutors::PDynamicThreadPool GetDynamicThreadPool();
    NCorbaHelpers::IContainerNamed* GetContainerNamed();

    bool IsCurrentTestFailed() const noexcept;
};

boost::shared_ptr<IPINT30::IIpintDevice> CreateFakeIpintDevice(std::shared_ptr<ITV8::GDRV::IDevice> device);

class MockRecordingSearch;
std::shared_ptr<ITV8::GDRV::IDevice> CreateTestIpintDevice(std::shared_ptr<MockRecordingSearch> mockRecordingSearch,
    ITV8::Utility::RecordingInfoSP recordingInfo);





}} // namespace ::UnitTesting
