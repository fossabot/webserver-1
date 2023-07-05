
#include <grpcpp/impl/codegen/status_code_enum.h>
#include <google/protobuf/util/time_util.h>
#include <DeviceIpint_3/DeviceSettings.h>
#include <boost/test/unit_test.hpp>
#include <grpcpp/client_context.h>
#include <Basic_Types.h>

#include <InfraServer_IDL/SecurityManagerC.h>
#include <InfraServer_IDL/SecurityManagerS.h>

#include <CorbaHelpers/ResolveServant.h>
#include <CorbaHelpers/Unicode.h>

#include <thread>
#include <chrono>
#include <ctime>

#include "../CTelemetry.h"
#include "TestUtils.h"

namespace TelemetryTest
{

class TestTelemetryFixture : public DeviceIpint_3::UnitTesting::BasicFixture
{

public:

    TestTelemetryFixture() {}
    ~TestTelemetryFixture() {}
};

BOOST_FIXTURE_TEST_SUITE(TestCTelemetry, TestTelemetryFixture)

void checkTimeOutVal(IPINT30::CTelemetry& telemetryInstanceRef, int expectedTimeDiff) {
    const NMMSS::UserSessionInformation userSession(L"m_userName", L"m_hostName");
    long priority = 0;
    NMMSS::UserSessionInformation blockingUserInformation; 
    time_t expirationTime;
    long result; 
    telemetryInstanceRef.AcquireSessionId(userSession, priority, result, blockingUserInformation, expirationTime);
    time_t expiratonTimeCheckCall = telemetryInstanceRef.GetSessionExpirationTime(result);
    BOOST_CHECK_EQUAL(expirationTime, expiratonTimeCheckCall);
    time_t currentTime = time(nullptr);
    BOOST_ASSERT(expirationTime > currentTime);
    time_t timeoutDiffInSeconds = expirationTime - currentTime;
    BOOST_CHECK((timeoutDiffInSeconds) >= (expectedTimeDiff - 1));
    BOOST_CHECK((timeoutDiffInSeconds) <= (expectedTimeDiff + 1));
}

BOOST_AUTO_TEST_CASE(TestCTelemetry_sessionTimeoutBasicTest)
{
    IPINT30::STelemetryParam telSettings;
    IPINT30::CTelemetry telemetryInstance(
        GetLogger(), 
        GetDynamicThreadPool(), 
        nullptr, 
        telSettings,
        nullptr,
        "context",
        m_contNamed.Get()
        );
    const int actualTimeValueThatBeingSet = 10;
    const int period = 4;
    int totalSleepTime = 0;
    for (int i = 0 ; i <= 12; i+=period) {
        std::this_thread::sleep_for(std::chrono::seconds(period));
        if(totalSleepTime > actualTimeValueThatBeingSet)
            totalSleepTime = 0;
        checkTimeOutVal(telemetryInstance, actualTimeValueThatBeingSet - totalSleepTime);
        totalSleepTime = totalSleepTime + period;
    }
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace TelemetryTest