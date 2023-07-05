#pragma once

#include <memory>
#include <utility>
#include <vector>

class CudaDevice;
using CudaDeviceSP = std::shared_ptr<CudaDevice>;

class CudaMemory;
using CudaMemorySP = std::shared_ptr<CudaMemory>;

class CudaSharedMemory;
using CudaSharedMemorySP = std::shared_ptr<CudaSharedMemory>;

class CudaMemoryLease;
using CudaMemoryLeaseSP = std::shared_ptr<CudaMemoryLease>;

class CudaStream;
using CudaStreamSP = std::shared_ptr<CudaStream>;

class CudaSampleHolder;
using CudaSampleHolderSP = std::shared_ptr<CudaSampleHolder>;

class CudaProcessor;

class AscendDevice;
using AscendDeviceSP = std::shared_ptr<AscendDevice>;

class IHWDevice;
using HWDeviceSP = std::shared_ptr<IHWDevice>;

class IHWDecoderFactory;
using HWDecoderFactorySP = std::shared_ptr<IHWDecoderFactory>;

class CompressedData;
class IHWDecoder;
using IHWDecoderSP = std::shared_ptr<IHWDecoder>;
class IHWReceiver;
using IHWReceiverSP = std::shared_ptr<IHWReceiver>;

class IDecoderPerformance;
using DecoderPerformanceSP = std::shared_ptr<IDecoderPerformance>;

class DecoderPerformancePool;
using DecoderPerformancePoolSP = std::shared_ptr<DecoderPerformancePool>;

class QSDevice;
using QSDeviceSP = std::shared_ptr<QSDevice>;
class HiddenDxDevice;
using HiddenDxDeviceSP = std::shared_ptr<HiddenDxDevice>;

class VideoPerformanceInfo
{
public:
    bool IsEmpty() const
    {
        return !Width || !Height || !Fps;
    }

public:
    uint32_t Codec{};
    int Width{};
    int Height{};
    float Fps{};
    bool Forced{};
    std::vector<std::pair<const char*, float>> Extra;
};
