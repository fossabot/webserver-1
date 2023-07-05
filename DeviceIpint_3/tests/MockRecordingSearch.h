#pragma once

#include <map>
#include <memory>

#include <CorbaHelpers/Reactor.h>
#include <Primitives/Executors/DynamicThreadPool.h>
#include <Primitives/CorbaHelpers/OrbMainTestHelper.h>
#include <ItvDeviceSdk/include/IStorageDevice.h>
#include <ItvDeviceSdk/include/IRecordingSearch.h>

#include "../StorageDataTypes.h"


namespace DeviceIpint_3 { namespace UnitTesting {

       
typedef std::map<uint32_t/*timeout*/, ITV8::Utility::tracksRangeList_t /*reportng tracks range*/> reportingTracks_t;

class MockRecordingSearch 
{
    DECLARE_LOGGER_HOLDER;
    reportingTracks_t m_reportingTracks;

public:
    MockRecordingSearch(DECLARE_LOGGER_ARG);

    ITV8::GDRV::IRecordingSearch* createRecordingsSearch(const char* recordingId);

    void setReportingTracks(const reportingTracks_t& t) { m_reportingTracks = t; }
    const reportingTracks_t& getReportingTracks() const { return m_reportingTracks; }
    void setSearchWillFail(bool value) { m_searchFail = value; }
    bool getSearchWillFail() const { return m_searchFail; }

private:
    bool m_searchFail;
};

}} 
