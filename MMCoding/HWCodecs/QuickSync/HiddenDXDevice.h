#pragma once

#include "QSDevice.h"
#include <atlbase.h>

struct ID3D11Device;
class D3DWrapper;

class HiddenDxDevice : public QSDevice
{
public:
    HiddenDxDevice();
    ~HiddenDxDevice();

    void Init(int adapterNum) override;
    bool IsValid() const override;

    CComPtr<ID3D11Device> GetDevice();
    std::shared_ptr<D3DWrapper> GetWrapper();

private:
    CComPtr<ID3D11Device> createDevice(int adapterNum);

private:
    CComPtr<ID3D11Device> m_device;
    std::shared_ptr<D3DWrapper> m_wrapper;
};
