#include "ItvSdkUtil.h"
#include <itv-sdk/ItvMediaSdk/include/frameFactorySdk.h>
#include <Logging/log2.h>
#include <string>
#include <vector>

namespace
{

    class CBlobBuffer : public ITV8::MFF::IBlobBuffer
    {
    public:
        ITV8_BEGIN_CONTRACT_MAP()
            ITV8_CONTRACT_ENTRY(ITV8::IContract)
            ITV8_CONTRACT_ENTRY(ITV8::MFF::IMultimediaBuffer)
            ITV8_CONTRACT_ENTRY(ITV8::MFF::IBlobBuffer)
        ITV8_END_CONTRACT_MAP()

    public:
        CBlobBuffer(ITV8::timestamp_t timestamp, const char* name, ITV8::uint32_t size)
            : m_timestamp(timestamp)
            , m_name(name)
            , m_data(size)
        {
        }
        ITV8::timestamp_t GetTimeStamp() override
        {
            return m_timestamp;
        }
        const char* GetName() override
        {
            return m_name.c_str();
        }
        ITV8::MFF::BufferTypes GetBufferType() override
        {
            return ITV8::MFF::ByContract;
        }
        void Destroy() override
        {
            delete this;
        }
        ITV8::uint8_t* GetData() override
        {
            return m_data.data();
        }
        ITV8::uint32_t GetSize() override
        {
            return m_data.size();
        }
    private:
        ITV8::timestamp_t const m_timestamp;
        std::string const m_name;
        std::vector<ITV8::uint8_t> m_data;
    };

    class CBlobFrameFactory : public ITV8::MFF::IBlobFrameFactory
    {
    public:
        ITV8_BEGIN_CONTRACT_MAP()
            ITV8_CONTRACT_ENTRY(ITV8::IContract)
            ITV8_CONTRACT_ENTRY(ITV8::MFF::IBlobFrameFactory)
        ITV8_END_CONTRACT_MAP()
    public:
        CBlobFrameFactory(DECLARE_LOGGER_ARG)
        {
        }
        ITV8::MFF::IBlobBuffer* AllocateBlobFrame(
            ITV8::timestamp_t timestamp, const char* name, ITV8::uint32_t size) override
        {
            return new CBlobBuffer(timestamp, name, size);
        }
    };

} // anonymous namespace

namespace ITVSDKUTILES
{

    IBlobFrameFactoryPtr CreateBlobFrameFactory(DECLARE_LOGGER_ARG)
    {
        return IBlobFrameFactoryPtr(new CBlobFrameFactory(GET_LOGGER_PTR));
    }

} // ITVSDKUTILES
