#ifndef ITVSDKUTIL_CCOMPOSITEBUFFER_H
#define ITVSDKUTIL_CCOMPOSITEBUFFER_H

#include "MediaBufferImpl.h"
#include "../mIntTypes.h"

class CCompositeBuffer : 
    public MediaBufferImpl<ITV8::MFF::ICompositeBuffer, ITV8::MFF::Composite>
{

// IContract implementation
public:
    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY2(ITV8::IContract, ITV8::MFF::ICompositeBuffer)
        ITV8_CONTRACT_ENTRY(ITV8::MFF::IMultimediaBuffer)
        ITV8_CONTRACT_ENTRY(ITV8::MFF::ICompositeBuffer)
        ITV8_CONTRACT_ENTRY(ITVSDKUTILES::ISampleContainer)
    ITV8_END_CONTRACT_MAP()

public:
    explicit CCompositeBuffer(NMMSS::ISample const* sample, char const* name);
    explicit CCompositeBuffer(NMMSS::ISample* sample, ::uint32_t minorID, 
		char const* name, ITV8::uint32_t bufferSize, ITV8::int32_t stride, ITV8::timestamp_t timestamp, 
        ITV8::uint32_t width, ITV8::uint32_t height);

    virtual ~CCompositeBuffer();

// ITV8::MFF::ICompositeBuffer implementation
public:
    virtual ITV8::uint8_t* GetBuffer();
    virtual ITV8::uint32_t GetBufferSize();
    virtual ITV8::uint32_t GetWidth();
    virtual ITV8::uint32_t GetHeight();

};

#endif // ITVSDKUTIL_CCOMPOSITEBUFFER_H
