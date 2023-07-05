#ifndef ITVSDKUTIL_CAUDIOPCMBUFFER_H
#define ITVSDKUTIL_CAUDIOPCMBUFFER_H

#include "MediaBufferImpl.h"
#include "../mIntTypes.h"

class CAudioPcmBuffer :
	public MediaBufferImpl<ITV8::MFF::IAudioBufferPcm, ITV8::MFF::AudioPcm>
{
	// IContract implementation
public:
	ITV8_BEGIN_CONTRACT_MAP()
		ITV8_CONTRACT_ENTRY2(ITV8::IContract, ITV8::MFF::IAudioBufferPcm)
		ITV8_CONTRACT_ENTRY(ITV8::MFF::IMultimediaBuffer)
		ITV8_CONTRACT_ENTRY(ITV8::MFF::IAudioBufferPcm)
		ITV8_CONTRACT_ENTRY(ITV8::MFF::IAudioBuffer)
		ITV8_CONTRACT_ENTRY(ITVSDKUTILES::ISampleContainer)
	ITV8_END_CONTRACT_MAP()

public:

	explicit CAudioPcmBuffer(NMMSS::ISample const* sample, char const* name);
	explicit CAudioPcmBuffer(NMMSS::ISample* sample, ::uint32_t minorID, char const* name, 
		ITV8::uint32_t bufferSize, ITV8::timestamp_t timestamp, 
		ITV8::uint32_t sampleRate, ITV8::uint32_t bitPerSample, ITV8::uint32_t channels);

	virtual ~CAudioPcmBuffer();

	// ITV8::MFF::IAudioBufferPcm implementation
public:
	virtual ITV8::uint8_t* GetBuffer();
	virtual ITV8::uint32_t GetBufferSize();

	virtual ITV8::uint32_t	GetNumChannels();
	virtual ITV8::uint32_t	GetSampleRate();
	virtual ITV8::uint32_t	GetBitsPerSample();
};

#endif // ITVSDKUTIL_CAUDIOPCMBUFFER_H
