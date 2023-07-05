#ifndef DATA_BUFFER_H__
#define DATA_BUFFER_H__

#include <memory>
#include <cstdint>
#include <functional>

namespace NMMSS
{
    class ISample;
}

namespace NHttp
{
    struct IDataBuffer
    {
        virtual ~IDataBuffer() {}

        virtual std::uint8_t* GetData() const = 0;
        virtual std::uint64_t GetSize() const = 0;
        virtual std::uint64_t GetTimestamp() const = 0;
        virtual bool IsKeyData() const = 0;
        virtual bool IsEoS() const = 0;
        virtual bool IsUrgent() const = 0;
        virtual bool IsBinary() const = 0;
    };

    using PDataBuffer = std::shared_ptr<IDataBuffer>;

    using DataFunction = std::function<void(NHttp::PDataBuffer)>;

    PDataBuffer CreateDataBufferFromSample(NMMSS::ISample*, bool urgent = false);
    PDataBuffer CreateEoSBuffer(std::uint64_t);
}

#endif // !DATA_BUFFER_H__
