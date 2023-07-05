#include <ItvDeviceSdk/include/IStorageDevice.h>

#include "../StorageDataTypes.h"

#include "TestUtils.h"
#include "MockRecordingSearch.h"


namespace
{
using namespace ITV8;
using namespace ITV8::GDRV;
using namespace DeviceIpint_3::UnitTesting;

struct TestIpintDevice : public IDevice
                       , public IStorageDevice
{

    TestIpintDevice(std::shared_ptr<MockRecordingSearch> mockRecordingSearch, ITV8::Utility::RecordingInfoSP recordingInfo)
        : m_mockRecordingSearch(mockRecordingSearch)
        , m_recordingInfo(recordingInfo)
    {}

    // IDevice
    void Connect(const char*) override {};

    void Disconnect() override {};

    void Destroy() override {};

    IVideoSource* CreateVideoSource(IVideoSourceHandler*, uint32_t, uint32_t, MFF::IMultimediaFrameFactory*) override { return nullptr; }

    IAudioSource* CreateAudioSource(IAudioSourceHandler*, uint32_t, MFF::IMultimediaFrameFactory*) override { return nullptr; }

    IAudioDestination* CreateAudioDestination(IAudioDestinationHandler*, uint32_t) override { return nullptr; }

    IIODevice* CreateIODevice(IIODeviceHandler*, uint32_t) override { return nullptr; }

    ITelemetry* CreateTelemetryChannel(uint32_t) override { return nullptr; }

    ISerialPort* CreateSerialPort(ISerialPortHandler*, uint32_t) override { return nullptr; }

    IVideoAnalytics* CreateVideoAnalytics(IVideoAnalyticsHandler*, uint32_t, const char*) override { return nullptr; }

    IStorageSource* CreateStorageSource(IStorageSourceHandler*, uint32_t) override { return nullptr; }

    IUnit* CreateUnit(IAsyncDeviceChannelHandler*, IUnit*, const char*, const char*) override { return nullptr; }

    // IStorageDevice
    void RequestRecordingsInfo(IRecordingsInfoHandler* handler) override
    {
        // TODO
    }

    void RequestRecordingInfo(const char* recordingToken, ISingleRecordingInfoHandler* handler) override
    {
        if (m_recordingInfo && std::strcmp(m_recordingInfo->GetId(), recordingToken) == 0)
            return handler->RequestRecordingDone(this, *m_recordingInfo);

        return handler->Failed(static_cast<IStorageDevice*>(this), EUnsupportedCommand);
    }

    IRecordingSearch* CreateRecordingSearch(const char* recordingId) override
    {
        return m_mockRecordingSearch->createRecordingsSearch(recordingId);
    }


    IRecordingSource* CreateRecordingSource(const char*, MFF::IMultimediaFrameFactory*) override
    {
        return nullptr;
    }


    IRecordingPlayback* CreateRecordingPlayback(const char*, MFF::IMultimediaFrameFactory*, Storage::ITrackIdEnumerator&, IRecordingPlaybackHandler*) override
    {
        return nullptr;
    }

    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY2(ITV8::IContract, ITV8::GDRV::IStorageDevice)
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::IStorageDevice)
    ITV8_END_CONTRACT_MAP()

private:
    std::shared_ptr<MockRecordingSearch> m_mockRecordingSearch;
    Utility::RecordingInfoSP m_recordingInfo;
};
}

namespace DeviceIpint_3 { namespace UnitTesting {

std::shared_ptr<ITV8::GDRV::IDevice> CreateTestIpintDevice(std::shared_ptr<MockRecordingSearch> mockRecordingSearch,
    ITV8::Utility::RecordingInfoSP recordingInfo)
{
    return std::make_shared<TestIpintDevice>(mockRecordingSearch, recordingInfo);
}
}};
