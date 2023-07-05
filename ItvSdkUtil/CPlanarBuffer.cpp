#include "CPlanarBuffer.h"
#include "../Sample.h"
#include "../MediaType.h"
using namespace std;
using namespace ITV8;


class CPlanarBufferHelper
{
	bool m_isGrey;
	bool m_isI420;
	bool m_isY42B;
	NMMSS::NMediaType::Video::SVideoHeader*			m_greyHeader;
	NMMSS::NMediaType::Video::SYUVPlanarVideoHeader*	m_planarHeader;
public:
	CPlanarBufferHelper(NMMSS::ISample* sample)
		: m_isGrey(false), m_isI420(false), m_isY42B(false)
	{
		NMMSS::NMediaType::ApplyMediaTypeVisitor(sample, *this);
	}
	template<typename TMediaTypeHeader>
	void operator()(TMediaTypeHeader* pHeader, ::uint8_t* pData)
	{
	}
	void operator()(NMMSS::NMediaType::Video::fccGREY::SubtypeHeader* pHeader, ::uint8_t*)
	{
		m_greyHeader = pHeader;
		m_isGrey = true;
	}
	void operator()(NMMSS::NMediaType::Video::fccI420::SubtypeHeader* pHeader, ::uint8_t*)
	{
		m_greyHeader = pHeader;
		m_planarHeader = pHeader;
		m_isI420 = true;
	}
	void operator()(NMMSS::NMediaType::Video::fccY42B::SubtypeHeader* pHeader, ::uint8_t*)
	{
		m_greyHeader = pHeader;
		m_planarHeader = pHeader;
		m_isY42B = true;
	}
public:
	//results
	bool IsGrey()const
	{
		return m_isGrey;
	}
	bool IsI420()const
	{
		return m_isI420;
	}
	bool IsY42B()const
	{
		return m_isY42B;
	}
	NMMSS::NMediaType::Video::SVideoHeader* GetVideoHeader()
	{
		return m_greyHeader;
	}
	NMMSS::NMediaType::Video::SYUVPlanarVideoHeader* GetPlanarVideoHeader()
	{
		return m_planarHeader;
	}
};

NMMSS::NMediaType::Video::SVideoHeader* CPlanarBuffer::GetVideoHeader()
{
	return CPlanarBufferHelper(GetSample()).GetVideoHeader();
}
NMMSS::NMediaType::Video::SYUVPlanarVideoHeader* CPlanarBuffer::GetPlanarVideoHeader()
{
	return CPlanarBufferHelper(GetSample()).GetPlanarVideoHeader();
}


CPlanarBuffer::CPlanarBuffer(DECLARE_LOGGER_ARG, NMMSS::ISample const* sample, char const* name)
    :TBase(sample, name)
{
	INIT_LOGGER_HOLDER;
    using namespace NMMSS::NMediaType;

    switch (sample->Header().nSubtype)
    {
        case Video::fccI420::ID:    //    YUV420
        case Video::fccY42B::ID:    //    YUV422
		case Video::fccGREY::ID:    //    YUV400
#ifdef _DEBUG
			TBase::DebugViewSubheader(Video::ID, sample->Header().nSubtype);
#endif
			break;
        default:

            throw runtime_error("Can't create PlanarBuffer: Incorrect or unknown planar format");
    }
}

CPlanarBuffer::CPlanarBuffer(DECLARE_LOGGER_ARG, NMMSS::ISample* sample, ::uint32_t minorID, char const* name,
    const SArgs& args, ITV8::timestamp_t timestamp, 
    ITV8::uint32_t width, ITV8::uint32_t height)
    :TBase(sample, name, timestamp, args.YSize + args.USize + args.VSize)//, 
{
    const ITV8::uint32_t& YSize = args.YSize;
    const ITV8::uint32_t& USize = args.USize;
    const ITV8::uint32_t& VSize = args.VSize;
    const ITV8::uint32_t& strideY = args.strideY;
    const ITV8::uint32_t& strideU = args.strideU;
    const ITV8::uint32_t& strideV = args.strideV;

	INIT_LOGGER_HOLDER;
    using namespace NMMSS::NMediaType;

	assert(YSize == strideY*height);
	if(YSize != strideY*height)
	{
		_err_ << "CPlanarBuffer: YSize != strideY*height" << std::endl;
	}
    //    fccI420 and fccY42B planar formats 
    if ((minorID == Video::fccY42B::ID)||(minorID == Video::fccI420::ID))
    {
        Video::SYUVPlanarVideoHeader * planarHeader = 0;

        if (minorID == Video::fccY42B::ID)      //    YUV422
        {
			assert(USize == strideU*height);
			if(USize != strideU*height)
			{
				_err_ << "CPlanarBuffer: USize != strideU*height" << std::endl;
			}
			assert(VSize == strideV*height);
			if(VSize != strideV*height)
			{
				_err_ << "CPlanarBuffer: VSize != strideV*height" << std::endl;
			}

            MakeMediaTypeStruct<Video::fccY42B>
                (sample->GetHeader(),(Video::fccY42B::SubtypeHeader **)&planarHeader);
        }
        else if (minorID == Video::fccI420::ID)//    YUV420
        {
			assert(USize == strideU*height / 2);
			if(USize != strideU*height/2)
			{
				_err_ << "CPlanarBuffer: USize != strideU*height/2" << std::endl;
			}
			assert(VSize == strideV*height / 2);
			if(VSize != strideV*height/2)
			{
				_err_ << "CPlanarBuffer: VSize != strideV*height/2" << std::endl;
			}

            MakeMediaTypeStruct<Video::fccI420>
                (sample->GetHeader(), (Video::fccI420::SubtypeHeader **)&planarHeader);
        }
        else
        {
            throw runtime_error("Can't create PlanarBuffer: Incorrect or unknown planar format");
        }

        //    SVideoHeader        
        planarHeader->nWidth = width;
        planarHeader->nHeight = height;
        planarHeader->nPitch = strideY;
        planarHeader->nOffset = 0;

        //    SYUVPlanarVideoHeader
        planarHeader->nOffsetU = YSize;
        planarHeader->nPitchU = strideU;
        planarHeader->nOffsetV = YSize + USize;
        planarHeader->nPitchV = strideV;
    }
	// fccGREY as planar format support
	else if(minorID == Video::fccGREY::ID)//    YUV400
	{
		Video::SVideoHeader* greyHeader = 0;
		MakeMediaTypeStruct<Video::fccGREY>(sample->GetHeader(),(Video::fccGREY::SubtypeHeader**)&greyHeader);
		
		//    SVideoHeader        
		greyHeader->nWidth = width;
		greyHeader->nHeight = height;
		greyHeader->nPitch = strideY;
        greyHeader->nOffset = 0;
	}
    else
    {
        throw runtime_error("Can't create PlanarBuffer: Incorrect or unknown planar format");
    }
}

CPlanarBuffer::~CPlanarBuffer()
{
}

ITV8::uint8_t * CPlanarBuffer::GetBufferY()
{
    return GetSample()->GetBody();
}

ITV8::uint8_t * CPlanarBuffer::GetBufferU()
{
	if(	CPlanarBufferHelper(GetSample()).IsGrey() )
		return 0;

	return GetBufferY() + GetBufferYSize();
}

ITV8::uint8_t * CPlanarBuffer::GetBufferV()
{
	if(	CPlanarBufferHelper(GetSample()).IsGrey() )
		return 0;

	return GetBufferU() + GetBufferUSize();
}

ITV8::uint32_t CPlanarBuffer::GetBufferYSize()
{
	CPlanarBufferHelper helper(GetSample());
	if(	helper.IsGrey() )
	{
		return GetSample()->Header().nBodySize;
	}
	else if( helper.IsI420() || helper.IsY42B() )
	{
		return helper.GetPlanarVideoHeader()->nOffsetU;
	}
	assert(false);
	return 0;
}

ITV8::uint32_t CPlanarBuffer::GetBufferUSize()
{
	CPlanarBufferHelper helper(GetSample());
	if(	helper.IsGrey() )
	{
		return 0;
	}
	else if( helper.IsI420() || helper.IsY42B() )
	{
		return helper.GetPlanarVideoHeader()->nOffsetV - helper.GetPlanarVideoHeader()->nOffsetU;
	}
	assert(false);
    return 0;
}

ITV8::uint32_t CPlanarBuffer::GetBufferVSize()
{
	CPlanarBufferHelper helper(GetSample());
	if(	helper.IsGrey() )
	{
		return 0;
	}
	else if( helper.IsI420() || helper.IsY42B() )
	{
		return GetSample()->Header().nBodySize - helper.GetPlanarVideoHeader()->nOffsetV;
	}
	assert(false);
	return 0;
}

ITV8::uint32_t CPlanarBuffer::GetStrideY()
{
	return CPlanarBufferHelper(GetSample()).GetVideoHeader()->nPitch;
}

ITV8::uint32_t CPlanarBuffer::GetStrideU()
{
	CPlanarBufferHelper helper(GetSample());
	if(helper.IsGrey())
		return 0;
	return helper.GetPlanarVideoHeader()->nPitchU;
}

ITV8::uint32_t CPlanarBuffer::GetStrideV()
{
	CPlanarBufferHelper helper(GetSample());
	if(helper.IsGrey())
		return 0;
	return helper.GetPlanarVideoHeader()->nPitchV;
}

ITV8::uint32_t CPlanarBuffer::GetWidth()
{
    return CPlanarBufferHelper(GetSample()).GetVideoHeader()->nWidth;
}

ITV8::uint32_t CPlanarBuffer::GetHeight()
{
    return CPlanarBufferHelper(GetSample()).GetVideoHeader()->nHeight;
}
