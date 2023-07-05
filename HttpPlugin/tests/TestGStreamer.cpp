#include <thread>
#include <chrono>

#include <boost/test/unit_test.hpp>

#include <Logging/log2.h>
#include "../Gstreamer.h"

BOOST_AUTO_TEST_SUITE(HttpPlugin)

/* ACR-38673: improper Gstreamer shutdown crashes MMSS process on exit.
 * Crash happens in ~GstManager, which is only called on singleton destruction
 * during process shutdown. The unit test cannot catch an error, but can induce
 * console_test_runner process crash

   (console_test_runner:28157): GLib-CRITICAL **: g_main_loop_quit: assertion 'g_atomic_int_get (&loop->ref_count) > 0' failed
   (console_test_runner:28157): GLib-CRITICAL **: g_main_loop_unref: assertion 'g_atomic_int_get (&loop->ref_count) > 0' failed
 */

/*
 * This unit test can prevent other tests that use GstManager singleton from
 * working.
 */
BOOST_AUTO_TEST_CASE(Gstreamer_StartStop)
{
    NLogging::PLogger logger{NLogging::CreateConsoleLogger(NLogging::LEVEL_DEBUG)};

    auto gstManager = NPluginHelpers::CreateGstManager(logger.Get());
    gstManager->Start();
    std::this_thread::sleep_for(std::chrono::seconds(1));
    gstManager->Stop();
    gstManager.reset();
}

BOOST_AUTO_TEST_SUITE_END()
