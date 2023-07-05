#ifndef ITVSDKUTIL_CAUDIOBUFFER_H
#define ITVSDKUTIL_CAUDIOBUFFER_H

#include "MediaBufferImpl.h"
#include "../mIntTypes.h"

class CAudioBuffer :
    public MediaBufferImpl<ITV8::MFF::IAudioBuffer, ITV8::MFF::AudioCompressed>
{
    // IContract implementation
public:
    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY2(ITV8::IContract, ITV8::MFF::IAudioBuffer)
        ITV8_CONTRACT_ENTRY(ITV8::MFF::IMultimediaBuffer)
        ITV8_CONTRACT_ENTRY(ITV8::MFF::IAudioBuffer)
        ITV8_CONTRACT_ENTRY(ITVSDKUTILES::ISampleContainer)
    ITV8_END_CONTRACT_MAP()

public:

    explicit CAudioBuffer(NMMSS::ISample const* sample, char const* name);
	explicit CAudioBuffer(NMMSS::ISample* sample, ::uint32_t minorID, const char* name,
		ITV8::uint32_t bufferSize, ITV8::timestamp_t timestamp);

    virtual ~CAudioBuffer();

    // ITV8::MFF::IAudioBuffer implementation
public:
    virtual ITV8::uint8_t* GetBuffer();
    virtual ITV8::uint32_t GetBufferSize();
};

#endif // ITVSDKUTIL_CAUDIOBUFFER_H
