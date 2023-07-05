#ifndef ITVSDKUTIL_CPLANARBUFFER_H
#define ITVSDKUTIL_CPLANARBUFFER_H

#include "MediaBufferImpl.h"
#include "../mIntTypes.h"
#include <Logging/log2.h>

class CPlanarBuffer : 
    public MediaBufferImpl<ITV8::MFF::IPlanarBuffer, ITV8::MFF::Planar>
{
// IContract implementation
public:
    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY2(ITV8::IContract, ITV8::MFF::IPlanarBuffer)
        ITV8_CONTRACT_ENTRY(ITV8::MFF::IMultimediaBuffer)
        ITV8_CONTRACT_ENTRY(ITV8::MFF::IPlanarBuffer)
        ITV8_CONTRACT_ENTRY(ITVSDKUTILES::ISampleContainer)
    ITV8_END_CONTRACT_MAP()

public:
    // ¬ынуждены упаковать праметры в структуру, т.к. boost::lambda::new_ptr поддерживает только
    // 9 аргументов дл€ конструктора.
    struct SArgs 
    {
        const ITV8::uint32_t YSize;
        const ITV8::uint32_t USize;
        const ITV8::uint32_t VSize;
        const ITV8::uint32_t strideY;
        const ITV8::uint32_t strideU;
        const ITV8::uint32_t strideV;
    };


    explicit CPlanarBuffer(DECLARE_LOGGER_ARG, NMMSS::ISample const* sample, char const* name);
    explicit CPlanarBuffer(DECLARE_LOGGER_ARG, NMMSS::ISample* sample, ::uint32_t minorID, char const* name,
        const SArgs& args, ITV8::timestamp_t timestamp, ITV8::uint32_t width, ITV8::uint32_t height);

    virtual ~CPlanarBuffer();

// ITV8::MFF::IPlanarBuffer implementation
public:
    virtual ITV8::uint8_t * GetBufferY();
    virtual ITV8::uint8_t * GetBufferU();
    virtual ITV8::uint8_t * GetBufferV();

    virtual ITV8::uint32_t GetBufferYSize();
    virtual ITV8::uint32_t GetBufferUSize();
    virtual ITV8::uint32_t GetBufferVSize();

    virtual ITV8::uint32_t GetStrideY();
    virtual ITV8::uint32_t GetStrideU();
    virtual ITV8::uint32_t GetStrideV();

    virtual ITV8::uint32_t GetWidth();
    virtual ITV8::uint32_t GetHeight();

private:
	NMMSS::NMediaType::Video::SVideoHeader* GetVideoHeader();
	NMMSS::NMediaType::Video::SYUVPlanarVideoHeader* GetPlanarVideoHeader();
	DECLARE_LOGGER_HOLDER;
};

#endif // ITVSDKUTIL_CPLANARBUFFER_H
