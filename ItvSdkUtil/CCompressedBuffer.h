#ifndef ITVSDKUTIL_CCOMPRESSEDBUFFER_H
#define ITVSDKUTIL_CCOMPRESSEDBUFFER_H

#include "MediaBufferImpl.h"
#include "../mIntTypes.h"
#include <Logging/log2.h>

namespace NMMSS 
{
	class IFrameBuilder;
}

class CCompressedBuffer :
    public MediaBufferImpl<ITV8::MFF::ICompressedBuffer, ITV8::MFF::Compressed>
{
// IContract implementation
public:
    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY2(ITV8::IContract, ITV8::MFF::ICompressedBuffer)
        ITV8_CONTRACT_ENTRY(ITV8::MFF::IMultimediaBuffer)
        ITV8_CONTRACT_ENTRY(ITV8::MFF::ICompressedBuffer)
        ITV8_CONTRACT_ENTRY(ITVSDKUTILES::ISampleContainer)
    ITV8_END_CONTRACT_MAP()

public:

    explicit CCompressedBuffer(NMMSS::ISample const *, char const *);
    explicit CCompressedBuffer(NMMSS::IFrameBuilder* frameBuilder, NMMSS::ISample* sample,
        ::uint32_t minorID, ::uint16_t vendor, ::uint16_t codec, char const* name, 
        ITV8::uint32_t bufferSize, ITV8::timestamp_t timestamp, ITV8::bool_t isKeyFrame);

    virtual ~CCompressedBuffer();

// ITVSDKUTILES::ISampleContainer implementation
public:
    virtual NMMSS::ISample *Detach();

// ITV8::MFF::ICompressedBuffer implementation
public:
    virtual ITV8::uint8_t * GetBuffer();
    virtual ITV8::uint32_t GetBufferSize();
    virtual ITV8::bool_t IsKeyFrame();

private:
    NCorbaHelpers::CAutoPtr<NMMSS::IFrameBuilder> m_frameBuilder;
};

#endif // ITVSDKUTIL_CCOMPRESSEDBUFFER_H
