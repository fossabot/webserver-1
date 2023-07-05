#include <boost/thread/condition_variable.hpp>
#include <boost/thread.hpp>
#include <boost/make_shared.hpp>
#include <boost/optional.hpp>
#include <boost/assert.hpp>

#include <boost/test/unit_test.hpp>

#include "../IIPManager3.h"
#include "../RecordingPlaybackFactory.h"

using namespace IPINT30;

namespace
{

class MockIpDevice : public IIpintDevice
{
    void Connect() override {};
    void Connected(ITV8::GDRV::IDevice* source) override {};
    void StateChanged(ITV8::GDRV::IDevice* source, uint32_t state) override {};
    void Disconnected(ITV8::GDRV::IDevice* source) override {};
    void Open() override {};
    void Close() override {};
    void GetInitialState(NStructuredConfig::TCategoryParameters& params) const override{};
    void OnChanged(const NStructuredConfig::TCategoryParameters& params, const NStructuredConfig::TCategoryParameters& removed) override {};
    void OnChanged(const NStructuredConfig::TCategoryParameters& meta) override {};
    void Failed(ITV8::IContract* pSource, ITV8::hresult_t error) override {};
    void* QueryContract(const char* szName) override { return nullptr; };
    const void* QueryConstContract(const char* szName) const override{ return nullptr; };
    void SwitchEnabled()override {};
    void ApplyChanges(ITV8::IAsyncActionHandler* handler)override {}
    IParamContextIterator* GetParamIterator() override { return nullptr; }
    ITV8::GDRV::IDevice* getDevice() const override { return nullptr; };
    std::string ToString() const override{ return std::string(); };
    bool IsGeneric() const override { return false; };
    void onChannelSignalRestored() override {};
    bool BlockConfiguration() const override { return true; };
};

class TestSandbox
{
public:
    TestSandbox()
        : m_dynExec(NExecutors::CreateDynamicThreadPool(NLogging::CreateLogger(), "DeviceIpint", NExecutors::IDynamicThreadPool::UNLIMITED_QUEUE_LENGTH, 2, 1024))
    {
    }

    ~TestSandbox()
    {
        m_dynExec->Shutdown();
    }

    IRecordingPlaybackSP run(ITV8::GDRV::IStorageDevice* device,
        const std::string& recordingId,
        ITV8::MFF::IMultimediaFrameFactory* frameFactory,
        ITV8::Utility::tracksList_t& tracks,
        ITV8::GDRV::IRecordingPlaybackHandler* handler)
    {
        boost::shared_ptr<IPINT30::IIpintDevice> parent(new MockIpDevice());
        RecordingPlaybackFactory factory(device, m_dynExec, recordingId, tracks, parent);
        return factory.create(frameFactory, handler);
    }

public:
    std::vector<int> m_frames;
    int m_queueSize;

private:
    NExecutors::PDynamicThreadPool m_dynExec;
};

class MockStorageDeviceBase : protected ITV8::GDRV::IStorageDevice
{
public:
    MockStorageDeviceBase() :
        m_callsCount(0),
        m_sandbox(0)
    {}
    virtual void RequestRecordingsInfo(ITV8::GDRV::IRecordingsInfoHandler* handler)
    {}

    virtual void RequestRecordingInfo(const char* recordingToken,
        ITV8::GDRV::ISingleRecordingInfoHandler* handler) { }
    virtual ITV8::GDRV::IRecordingSearch* CreateRecordingSearch(const char* recordingId) { return 0;}
    virtual ITV8::GDRV::IRecordingSource* CreateRecordingSource(const char* recordingId,
        ITV8::MFF::IMultimediaFrameFactory* factory) { return 0; }
    virtual ITV8::GDRV::IRecordingPlayback* CreateRecordingPlayback(const char* recordingId, ITV8::MFF::IMultimediaFrameFactory* factory,
        ITV8::GDRV::Storage::ITrackIdEnumerator& tracks, ITV8::GDRV::IRecordingPlaybackHandler* handler)
    {
        return 0;
    }

    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::IStorageDevice)
    ITV8_END_CONTRACT_MAP()

    ITV8::GDRV::IStorageDevice* getStorageDevice(TestSandbox* sandbox)
    {
        m_sandbox = sandbox;
        return this;
    }

public:
    int m_callsCount;
    TestSandbox* m_sandbox;
};

class MocPlayback : public ITV8::GDRV::IRecordingPlayback
{
    virtual void Teardown()
    {}

    virtual void Play(ITV8::double_t speed,
        ITV8::IAsyncActionHandler* handler)
    {}

    virtual void Seek(ITV8::timestamp_t time,
        ITV8::IAsyncActionHandler* handler) {}

    virtual void Step(ITV8::int32_t direction,
        ITV8::IAsyncActionHandler* handler){}

    virtual void Destroy()
    {}

    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::IRecordingPlayback)
    ITV8_END_CONTRACT_MAP()
};
}

BOOST_AUTO_TEST_SUITE(TestRecordingPlaybackFactory)

BOOST_AUTO_TEST_CASE(testInvalidStorageDevicePointer)
{
    TestSandbox sandbox;
    ITV8::Utility::tracksList_t list;
    IRecordingPlaybackSP playback = sandbox.run(0, "recording", 0, list, 0);
    BOOST_TEST_CHECK(!playback);
}

BOOST_AUTO_TEST_CASE(testFactoryReturnCorrectObject)
{
    class StorageDeviceBase : public MockStorageDeviceBase
    {
    public:
        virtual ITV8::GDRV::IRecordingPlayback* CreateRecordingPlayback(const char* recordingId, ITV8::MFF::IMultimediaFrameFactory* factory,
            ITV8::GDRV::Storage::ITrackIdEnumerator& tracks, ITV8::GDRV::IRecordingPlaybackHandler* handler)
        {
            ++m_callsCount;
            m_recordingId = recordingId;
            m_tracks = ITV8::Utility::enumerateTracks(&tracks);
            m_factory = factory;
            m_handler = handler;
            return new MocPlayback();
        }


        std::string m_recordingId;
        ITV8::MFF::IMultimediaFrameFactory* m_factory;
        ITV8::Utility::tracksList_t m_tracks;
        ITV8::GDRV::IRecordingPlaybackHandler* m_handler;
    };


    ITV8::Utility::tracksList_t list;
    list.push_back("test_track");

    ITV8::MFF::IMultimediaFrameFactory* factory = nullptr;
    ITV8::GDRV::IRecordingPlaybackHandler* handler = nullptr;
    std::string recording_id = "recording";

    StorageDeviceBase device;
    TestSandbox sandbox;
    IRecordingPlaybackSP playback = sandbox.run(
        device.getStorageDevice(&sandbox),
        recording_id, factory, list, handler);

    BOOST_CHECK_MESSAGE(playback, "Should not be empty");

    BOOST_TEST_INFO("Cals count should be 1");
    BOOST_CHECK_EQUAL(1, device.m_callsCount);

    BOOST_TEST_INFO("Factory Does not match");
    BOOST_CHECK_EQUAL(factory, device.m_factory);

    BOOST_TEST_INFO("Handler does not match");
    BOOST_CHECK_EQUAL(handler, device.m_handler);

    BOOST_TEST_INFO("Recording id does not match");
    BOOST_CHECK_EQUAL(recording_id, device.m_recordingId);

    BOOST_TEST_INFO("Tracks size does not match");
    BOOST_CHECK_EQUAL(list.size(), device.m_tracks.size());

    BOOST_TEST_INFO("Track is does not match");
    BOOST_CHECK_EQUAL(list.front(), device.m_tracks.front());
}

BOOST_AUTO_TEST_CASE(testFactoryReturnNull)
{
    class StorageDeviceBase : public MockStorageDeviceBase
    {
    public:
        virtual ITV8::GDRV::IRecordingPlayback* CreateRecordingPlayback(const char* recordingId, ITV8::MFF::IMultimediaFrameFactory* factory,
            ITV8::GDRV::Storage::ITrackIdEnumerator& tracks, ITV8::GDRV::IRecordingPlaybackHandler* handler)
        {
            ++m_callsCount;
            return 0;
        }
    };
    StorageDeviceBase device;
    TestSandbox sandbox;
    ITV8::Utility::tracksList_t list;
    IRecordingPlaybackSP playback = sandbox.run(
        device.getStorageDevice(&sandbox),
        "", 0, list, 0);
    BOOST_CHECK_MESSAGE(!playback, "Playback should be empty");

    BOOST_TEST_INFO("Cals count should be 1");
    BOOST_CHECK_EQUAL(1, device.m_callsCount);
}

BOOST_AUTO_TEST_SUITE_END() // TestRecordingPlaybackFactory
