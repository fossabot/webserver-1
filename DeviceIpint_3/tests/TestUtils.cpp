#include <boost/test/unit_test.hpp>
#include <boost/test/tree/test_unit.hpp>
#include <boost/test/results_collector.hpp>

#include <mmss/ConnectionBroker.h>

#include "TestUtils.h"

namespace DeviceIpint_3 { namespace UnitTesting {

BasicFixture::BasicFixture()
        : WithLogger(NLogging::PLogger(NLogging::CreateConsoleLogger(NLogging::LEVEL_DEBUG)).Get())
{
    GetLogger()->AddPrefix((boost::unit_test::framework::current_test_case().full_name()).c_str());
    m_ioPool = NExecutors::CreateDynamicThreadPool(GetLogger(), "DvIpInt3IO", 128, 0);
    m_orbMain = CreateOrbMain(GetLogger());
    m_rootContainer = m_orbMain->GetRoot();
    m_contNamed = NCorbaHelpers::PContainerNamed(m_rootContainer->CreateContainer("Test_DeviceIpint_3", NCorbaHelpers::IContainer::EA_DontAdvertise));
}

BasicFixture::~BasicFixture()
{
	m_ioPool->Shutdown();
	m_contNamed.Reset();
	m_orbMain.reset();
}

boost::asio::io_context& BasicFixture::GetIO()
{
    return GetReactor()->GetIO();
}

NExecutors::PDynamicThreadPool BasicFixture::GetDynamicThreadPool()
{
    return m_ioPool;
}

NCorbaHelpers::IContainerNamed* BasicFixture::GetContainerNamed()
{
    return m_contNamed.Get();
}

bool BasicFixture::IsCurrentTestFailed() const noexcept
{
    auto test_id = boost::unit_test::framework::current_test_case().p_id;
    return !boost::unit_test::results_collector.results(test_id).passed();
}


}};
