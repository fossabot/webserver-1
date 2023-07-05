#ifndef ITVSDKUTIL_CAUDIOG7XXBUFFER_H
#define ITVSDKUTIL_CAUDIOG7XXBUFFER_H

#include "MediaBufferImpl.h"
#include "../mIntTypes.h"

class CAudioG7xxBuffer :
public MediaBufferImpl<ITV8::MFF::IAudioBufferG7xxEx, ITV8::MFF::AudioG7xx>
{
    // IContract implementation
public:
    ITV8_BEGIN_CONTRACT_MAP()
    ITV8_CONTRACT_ENTRY2(ITV8::IContract, ITV8::MFF::IAudioBufferG7xxEx)
    ITV8_CONTRACT_ENTRY(ITV8::MFF::IMultimediaBuffer)
    ITV8_CONTRACT_ENTRY(ITV8::MFF::IAudioBufferG7xxEx)
    ITV8_CONTRACT_ENTRY(ITV8::MFF::IAudioBufferG7xx)
    ITV8_CONTRACT_ENTRY(ITV8::MFF::IAudioBuffer)
    ITV8_CONTRACT_ENTRY(ITVSDKUTILES::ISampleContainer)
    ITV8_END_CONTRACT_MAP()

public:

    explicit CAudioG7xxBuffer(NMMSS::ISample const* sample, char const* name);
    explicit CAudioG7xxBuffer(NMMSS::ISample* sample, ::uint32_t minorID, char const* name,
                              ITV8::uint32_t bufferSize, ITV8::timestamp_t timestamp,
                              ITV8::uint32_t bitRate, ITV8::uint32_t encoding);
    explicit CAudioG7xxBuffer(NMMSS::ISample* sample, ::uint32_t minorID, char const* name,
                              ITV8::uint32_t bufferSize, ITV8::timestamp_t timestamp,
                              ITV8::uint32_t bitRate, ITV8::uint32_t encoding, 
                              ITV8::uint32_t channelsNum, ITV8::uint32_t sampleRate,
                              ITV8::uint32_t bitsPerSample);

    virtual ~CAudioG7xxBuffer();

    // ITV8::MFF::IAudioBufferG7xxEx implementation
public:
    virtual ITV8::uint8_t* GetBuffer();
    virtual ITV8::uint32_t GetBufferSize();
    virtual ITV8::uint32_t GetBitRate();
    virtual ITV8::uint32_t GetEncoding();
    virtual ITV8::uint32_t GetNumChannels();
    virtual ITV8::uint32_t GetSampleRate();
    virtual ITV8::uint32_t GetBitsPerSample();

public:
    virtual void ConvertToHostBitOrder();
    virtual void ConvertToNetworkBitOrder();

private:
    enum EDirection
    {
        TO_HOST = 0,
        TO_NETWORK
    };

    template<EDirection>
    void ConvertBitOrder();

    template<int,EDirection>
    static int ConvertBytesGroup(ITV8::uint8_t* ptr);

    ITV8::uint32_t m_bitsPerSample;

};

#endif // ITVSDKUTIL_CAUDIOG7XXBUFFER_H
