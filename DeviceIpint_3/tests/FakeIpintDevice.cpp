#include "TestUtils.h"

#include <ItvSdk/include/baseTypes.h>

#include "../ParamIterator.h"

namespace
{
using namespace IPINT30;
using namespace ITV8::GDRV;

class FakeIpintDevice : public IPINT30::IIpintDevice

{
public:
    FakeIpintDevice(std::shared_ptr<ITV8::GDRV::IDevice> device)
        : m_device(device)
    {}

    // IObjectParamContext
    void SwitchEnabled() override {}
    void ApplyChanges(ITV8::IAsyncActionHandler*) override { }
    IParamContextIterator* GetParamIterator() override { return nullptr; }
    void ApplyMetaChanges(const NStructuredConfig::TCustomParameters&) override { };

    //IParameterHolder
    void GetInitialState(NStructuredConfig::TCategoryParameters&) const override { }
    void OnChanged(const NStructuredConfig::TCategoryParameters&, const NStructuredConfig::TCategoryParameters&) override { }
    void OnChanged(const NStructuredConfig::TCategoryParameters&) override { }
    void ResetToDefaultProperties(std::string&) override { }

    //IIPManager
    void Connect() override { }

    // IIpintDevice
    void Open() override { }
    void Close() override { }
    ITV8::GDRV::IDevice* getDevice() const override { return m_device.get(); }
    std::string ToString() const override { return std::string(); }
    bool IsGeneric() const override { return false; }
    void onChannelSignalRestored() override { }
    bool BlockConfiguration() const override { return true; }

    //IDeviceHandler
    void Connected(IDevice*) override {}
    void StateChanged(IDevice*, uint32_t) override {}
    void Disconnected(IDevice*) override {}
    void Failed(IContract*, ITV8::hresult_t) override {}

    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY2(ITV8::IContract, ITV8::GDRV::IDeviceHandler)
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::IDeviceHandler)
    ITV8_END_CONTRACT_MAP()
private:
    std::shared_ptr<ITV8::GDRV::IDevice> m_device;
};
}

namespace DeviceIpint_3 { namespace UnitTesting {

boost::shared_ptr<IPINT30::IIpintDevice> CreateFakeIpintDevice(std::shared_ptr<ITV8::GDRV::IDevice> device)
{
    return boost::make_shared<FakeIpintDevice>(device);
}

}};