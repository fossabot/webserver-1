#ifndef ITVSDKUTIL_CAUDIOBUFFERMPEG_H
#define ITVSDKUTIL_CAUDIOBUFFERMPEG_H

#include "MediaBufferImpl.h"
#include "../mIntTypes.h"
#include <vector>
#include "CFrameFactory.h"

namespace NMMSS 
{ 
	class IAllocator; 
}

class CAudioBufferMpeg :
	public MediaBufferImpl<ITV8::MFF::IAudioBufferMPEG, ITV8::MFF::AudioCompressed>
{
	static const int AAC_EXTRA_LENGTH = 2;
	// IContract implementation
public:
	ITV8_BEGIN_CONTRACT_MAP()
		ITV8_CONTRACT_ENTRY2(ITV8::IContract, ITV8::MFF::IAudioBufferMPEG)
		ITV8_CONTRACT_ENTRY(ITV8::MFF::IMultimediaBuffer)
		ITV8_CONTRACT_ENTRY(ITV8::MFF::IAudioBufferMPEG)
		ITV8_CONTRACT_ENTRY(ITVSDKUTILES::ISampleContainer)
	ITV8_END_CONTRACT_MAP()

public:
	explicit CAudioBufferMpeg(NMMSS::IAllocator* allocator, NMMSS::ISample* sample, ::uint32_t minorID, 
        const char* name, ITV8::uint32_t bufferSize, ITV8::timestamp_t timestamp, 
        ITV8::uint32_t extraSize, IExtraDataHolder* extraDataHolder);

	// ITV8::MFF::IAudioBuffer implementation
public:
	virtual ITV8::uint8_t* GetBuffer();
	virtual ITV8::uint32_t GetBufferSize();

	// ITV8::MFF::IAudioBufferMPEG implementation
public:
	/// Gets extra data (usually stream configuration).
	/// Extra data received from HTTP should be in decoded (binary) form.
	/// Returns pointer to the start of data.
	virtual ITV8::uint8_t*	GetExtraData();

	/// Gets extra data size.
	virtual ITV8::uint32_t	GetExtraSize();

public:
	virtual NMMSS::ISample *Detach();//override

private:
    std::vector<ITV8::uint8_t> m_extraBuffer;
    NCorbaHelpers::CAutoPtr<NMMSS::IAllocator> m_allocator;
    IExtraDataHolder* m_extraDataHolder;
};

#endif // ITVSDKUTIL_CAUDIOBUFFERMPEG_H
