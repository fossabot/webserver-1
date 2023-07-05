#ifndef ITVSDKUTIL_CMEDIABUFFER_H
#define ITVSDKUTIL_CMEDIABUFFER_H

#include "ISampleContainer.h"

#include "../Sample.h"
#include "../MediaType.h"
#include <ItvFramework/TimeConverter.h>
#include "../PtimeFromQword.h"
#include <ItvMediaSdk/include/frameFactorySdk.h>
#include <boost/noncopyable.hpp>
#include <string>

namespace NMMSS 
{ 
	class ISample; 
}

//    CMediaBuffer is intrusive wrapper about ISample

template<class T, ITV8::MFF::BufferTypes TBufferType>
class MediaBufferImpl : 
    private boost::noncopyable, 
    public T,
    public ITVSDKUTILES::ISampleContainer
{
protected:
    typedef MediaBufferImpl<T, TBufferType> TBase;

public:
    explicit MediaBufferImpl(NMMSS::ISample const* sample, std::string const& name)
        : m_sample(const_cast<NMMSS::ISample *>(sample))
        , m_name(name) 
    {
        m_sample->AddRef();
    }
    explicit MediaBufferImpl(NMMSS::ISample* sample, std::string const& name, 
        ITV8::timestamp_t timestamp, ITV8::uint32_t bufferSize)
        : m_sample(sample)
        , m_name(name)
    {
        m_sample->AddRef();
		NMMSS::SMediaSampleHeader& header = m_sample->Header();
		header.eFlags = 0;
        header.dtTimeBegin = NMMSS::PtimeToQword( ITV8::PtimeFromTimestamp(timestamp) );
		header.dtTimeEnd = header.dtTimeBegin + 10;
		header.nBodySize = bufferSize;
    }

    virtual ~MediaBufferImpl()
    {
        if(m_sample != 0)
        {
            m_sample->Release();
        }
        m_sample = 0;
    }

protected:
    NMMSS::ISample* GetSample() const
    {
        return m_sample;
    }

// ITVSDKUTILES::ISampleContainer implementation
public:
    virtual NMMSS::ISample *Detach()
    {
        NMMSS::ISample *sample = m_sample;
        m_sample = 0;
        return sample;
    }

// ITV8::MFF::IMultimediaBuffer implementation
public:
    virtual ITV8::timestamp_t GetTimeStamp()
    {
        return ITV8::PtimeToTimestamp(NMMSS::PtimeFromQword(m_sample->Header().dtTimeBegin));
    }

    virtual char const * GetName()
    {
        return m_name.c_str();
    }

    virtual ITV8::MFF::BufferTypes GetBufferType()
    {
        return TBufferType;
    }

    virtual void Destroy()
    {
        delete this;
    }
protected:
	void DebugViewSubheader(boost::uint32_t major, boost::uint32_t minor)
	{
		using namespace NMMSS::NMediaType;
		if(major == Video::ID)
		{
			switch (minor)
			{
			case Video::fccJPEG::ID:
				{
					Video::fccJPEG::SubtypeHeader const& subHeader = m_sample->SubHeader<Video::fccJPEG>();
				}
				break;
			case Video::fccJPEG2000::ID:
				{
					Video::fccJPEG2000::SubtypeHeader const& subHeader = m_sample->SubHeader<Video::fccJPEG2000>();
				}
				break;
			case Video::fccMPEG4::ID:
				{
					Video::fccMPEG4::SubtypeHeader const& subHeader = m_sample->SubHeader<Video::fccMPEG4>();
				}
				break;
            case Video::fccMPEG2::ID:
				{
					Video::fccMPEG2::SubtypeHeader const& subHeader = m_sample->SubHeader<Video::fccMPEG2>();
				}
				break;
			case Video::fccRGB::ID:
				{
					Video::fccRGB::SubtypeHeader const& subHeader	= m_sample->SubHeader<Video::fccRGB>();
				}
				break;
			case Video::fccRGBA::ID:
				{
					Video::fccRGBA::SubtypeHeader const& subHeader	= m_sample->SubHeader<Video::fccRGBA>();
				}
				break;
			case Video::fccGREY::ID:
				{
					Video::fccGREY::SubtypeHeader const& subHeader	= m_sample->SubHeader<Video::fccGREY>();
				}
				break;
			case Video::fccVendor::ID:
				{
					Video::fccVendor::SubtypeHeader const& subHeader	= m_sample->SubHeader<Video::fccVendor>();
				}
				break;
			}
		}
		else if(major == Audio::ID)
		{
			switch (minor)
			{
			case Audio::G711::ID:
				{
					Audio::G711::SubtypeHeader const& subHeader = m_sample->SubHeader<Audio::G711>();
				}
				break;
			case Audio::G726::ID:
				{
					Audio::G726::SubtypeHeader const& subHeader = m_sample->SubHeader<Audio::G726>();
				}
				break;
			case Audio::AAC::ID:
				{
					Audio::AAC::SubtypeHeader const& subHeader = m_sample->SubHeader<Audio::AAC>();
				}
				break;
			case Audio::GSM::ID:
				{
					Audio::GSM::SubtypeHeader const& subHeader = m_sample->SubHeader<Audio::GSM>();
				}
				break;
			case Audio::VORBIS::ID:
				{
					Audio::VORBIS::SubtypeHeader const& subHeader = m_sample->SubHeader<Audio::VORBIS>();
				}
				break;
			case Audio::PCM::ID:
				{
					Audio::PCM::SubtypeHeader const& subHeader = m_sample->SubHeader<Audio::PCM>();
				}
				break;
            case Audio::MP2::ID:
            {
                Audio::MP2::SubtypeHeader const& subHeader = m_sample->SubHeader<Audio::MP2>();
            }
            break;
			}
		}
	}


private:
    NMMSS::ISample * m_sample;
    std::string m_name;
};

#endif // ITVSDKUTIL_CMEDIABUFFER_H
