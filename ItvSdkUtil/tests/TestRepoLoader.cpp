#include <boost/test/unit_test.hpp>

#include <boost/filesystem.hpp>


#include <Logging/log2.h>
#include "../ItvSdkUtil.h"
#include <DeviceInfo/include/RepoLoader.h>

namespace
{
class Fixture
{
protected:
    ITVSDKUTILES::ILoggerPtr logger_;
    ITV8::GDRV::RepoLoader* loader_;

public:
    Fixture()
        : logger_(
              ITVSDKUTILES::CreateLogger(NLogging::CreateConsoleLogger(NLogging::LEVEL_DEBUG),
              (boost::unit_test::framework::current_test_case().full_name() + "/").c_str()))
        , loader_(ITVSDKUTILES::GetRepoLoader(logger_.get()))
    {
    }

    ~Fixture()
    {
        ITVSDKUTILES::ReleaseRepoLoader();
    }

    boost::filesystem::path find_rep()
    {
        namespace bfs = boost::filesystem;

        const bfs::path TEST_REP("NeuroTracker.rep");

        std::vector<bfs::path> PATHS = {
            bfs::path("."),
            bfs::path(__FILE__).remove_filename(),
        };

        char* mmss_home_ptr = getenv("MMSS_HOME");
        if (mmss_home_ptr != nullptr)
        {
            PATHS.emplace_back(bfs::path(mmss_home_ptr) / "DeviceInfo" / "tests");
        }

        auto it = std::find_if(PATHS.begin(), PATHS.end(),
            [&TEST_REP](const bfs::path& p) {
            boost::system::error_code ec;
            return bfs::exists(p / TEST_REP, ec);
        }
        );

        if (it == PATHS.end())
            throw std::runtime_error("Test .rep file not found " + TEST_REP.string());

        return (*it) / TEST_REP;
    }
};

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(RepoLoader, Fixture)

BOOST_AUTO_TEST_CASE(LoadSuccessfully)
{
    ::ITV8::GDRV::DriverInfo out {};
    auto rep_file = find_rep().generic_string();
    BOOST_REQUIRE_EQUAL(0, ITVSDKUTILES::LoadRepository(loader_, rep_file.c_str(), out));
}

BOOST_AUTO_TEST_SUITE_END()