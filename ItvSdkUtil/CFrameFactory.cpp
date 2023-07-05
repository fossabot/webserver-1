#include "CFrameFactory.h"
#include "CAudioBuffer.h"
#include "CAudioG7xxBuffer.h"
#include "CAudioPcmBuffer.h"
#include "CAudioBufferMpeg.h"
#include "CCompositeBuffer.h"
#include "CCompressedBuffer.h"
#include "CPlanarBuffer.h"
#include "../MediaType.h"
#include "../ConnectionBroker.h"
#include <boost/make_shared.hpp>
#include <boost/date_time.hpp>
#include <boost/bind.hpp>
#include <limits>
#include <ItvFramework/TimeConverter.h>
#include "../MMCoding/FrameBuilder.h"
#include <ItvMediaSdk/include/codecConstants.h>
#include <ItvFramework/TargetEnumeratorAdjuster.h>

using namespace ITV8::MFF;
using namespace boost;
using namespace NMMSS::NMediaType;


CFrameFactory::CFrameFactory(DECLARE_LOGGER_ARG, NMMSS::IAllocator* allocator, const char* name) :
    m_logPrefix(name),
    m_allocator(allocator, NCorbaHelpers::ShareOwnership()),
    m_invalidTimestampLogged(false)
{
    INIT_LOGGER_HOLDER;
}

CFrameFactory::~CFrameFactory()
{
}

void CFrameFactory::SetAllocator(NMMSS::IAllocator* allocator)
{
    //TODO: постараться избавиться от этого метода или обеспечить потокобезопасность
    assert(false);
    m_allocator = allocator;
}

IMultimediaBuffer * CreateFrameFromSample(DECLARE_LOGGER_ARG, NMMSS::ISample const* sample, char const* name)
{

    try
    {
        switch (sample->Header().nMajor)
        {
            case Audio::ID:

                switch (sample->Header().nSubtype)
                {
                case Audio::G711::ID:
                case Audio::G726::ID:
                        return new CAudioG7xxBuffer(sample, name);
                case Audio::PCM::ID:
                    return new CAudioPcmBuffer(sample, name);
                case Audio::GSM::ID:
                case Audio::AAC::ID:
                case Audio::VORBIS::ID:
                case Audio::MP2::ID:
                    return new CAudioBuffer(sample, name);
                }

            case Video::ID:

                switch (sample->Header().nSubtype)
                {
                case Video::fccJPEG::ID:
                case Video::fccJPEG2000::ID:
                case Video::fccMXPEG::ID:
                case Video::fccH264::ID:
                case Video::fccMPEG4::ID:
                case Video::fccMPEG2::ID:
                case Video::fccH264SVC::ID:
                case Video::fccH264SVCT::ID:
                case Video::fccH265::ID:
                case Video::fccVP8::ID:
                case Video::fccVP9::ID:
#ifdef _WIN32
                case Video::fccWXWL::ID:
#endif
                case Video::fccVendor::ID:
                    return new CCompressedBuffer(sample, name);

                case Video::fccY42B::ID:
                case Video::fccI420::ID:
                case Video::fccGREY::ID:
                    return new CPlanarBuffer(GET_LOGGER_PTR, sample, name);

                case Video::fccRGB::ID:
                case Video::fccRGBA::ID:
                    return new CCompositeBuffer(sample, name);
                }
        }
    }
    catch (const std::exception& e)
    {
        _err_ << "Error on create MultimediaBuffer from Sample: " << e.what() << std::endl;
    }
    catch (...)
    {
        _err_ << "Error on create MultimediaBuffer from Sample" << std::endl;
        throw;
    }
    return 0;
}

// Creates multimedia frame buffer instance of different types.
// R - the interface of creating media buffer;
// TMediaType - the struct defines the media frame format in NGP style;
// TCreateFrameFunction - the type of function creates the instance of multimedia frame buffer.
// name - the name of multimedia format for new multimedia buffer.
// bufferSize - the size of memory which should be allocated for multimedia data.
// createFrameFunction - the reference function creates the instance of multimedia frame buffer.
//  The functions should have signature:
//      R* CreateFrameFunction(NMMSS::ISample* sample, ::uint32_t subtype, ::uint16_t vendor,
//                             ::uint16_t codec, NMMSS::IFrameBuilder *builder);
//      sample - the Ngp multimedia sampe which frame buffer can use for storing data.
//      subtype - the type of multimedia format (MMSS_MAKEFOURCC('M','P','G','4'),
//                MMSS_MAKEFOURCC('J','P','E','G') ).
//      vendor - the identifier of vendor for MMSS_MAKEFOURCC('V','N','D','R') format.
//      codec - the identifier of codec for MMSS_MAKEFOURCC('V','N','D','R') format.
template<class R, typename TMediaType, class TCreateFrameFunction>
R *CFrameFactory::CreateTypedFrame(char const* name, ITV8::uint32_t bufferSize,
                                   TCreateFrameFunction createFrameFunction)
{
    if(!bufferSize)
    {
        _err_ << m_logPrefix << " CreateTypedFrame(...) for" << typeid(R).name() << " - Invalid argument:"
            " \"bufferSize\" must be not 0." << std::endl;
        return 0;
    }

    NMMSS::PSample sample(m_allocator->Alloc(bufferSize));
    if (!sample)
    {
        _err_ << m_logPrefix << " m_allocator->Alloc(" << bufferSize<< ") return 0." << std::endl;
        return 0;
    }

    const std::string sName(name ? name : "");
    if (m_lastName != name)
    {
        m_lastName = name;
        auto it = m_multimediaSpecificData.find(name);
        if (it != m_multimediaSpecificData.end())
        {
            m_currentDescriptor = it->second.m_descriptor;
            m_currentFrameBuilder = it->second.m_frameBuilder;
        }
        else
        {
            m_currentDescriptor = ITVSDKUTILES::GetMediaFormatDictionary(GET_LOGGER_PTR).Find(name);
            if (!m_currentDescriptor->IsValid())
            {
                _err_ << m_logPrefix << " " << typeid(R).name() << " media format " << name
                    << " not supported." << std::endl;
                return 0;
            }

            if (m_currentDescriptor->GetMajorID() != TMediaType::ID)
            {
                _err_ << m_logPrefix << " Media format " << name
                    << " doesn't belong to " << typeid(TMediaType).name() << " category." << std::endl;
                return 0;
            }

            switch (m_currentDescriptor->GetMajorID())
            {
            case Video::ID:
            {
                switch (m_currentDescriptor->GetSubtype())
                {
                case Video::fccJPEG::ID:
                    m_currentFrameBuilder = NMMSS::CreateJPEGFrameBuilder();
                    break;
                case Video::fccJPEG2000::ID:
                    m_currentFrameBuilder = NMMSS::CreateJPEG2000FrameBuilder();
                    break;
                case Video::fccMXPEG::ID:
                    m_currentFrameBuilder = NMMSS::CreateMXPEGFrameBuilder();
                    break;
                case Video::fccMPEG2::ID:
                    m_currentFrameBuilder = NMMSS::CreateMPEG2FrameBuilder();
                    break;
                case Video::fccMPEG4::ID:
                    m_currentFrameBuilder = NMMSS::CreateMPEG4FrameBuilder();
                    break;
                case Video::fccH264::ID:
                    m_currentFrameBuilder = NMMSS::CreateH264FrameBuilder();
                    break;
                case Video::fccH265::ID:
                    m_currentFrameBuilder = NMMSS::CreateH265FrameBuilder();
                    break;
                case Video::fccVP8::ID:
                    m_currentFrameBuilder = NMMSS::CreateVP8FrameBuilder();
                    break;
                case Video::fccVP9::ID:
                    m_currentFrameBuilder = NMMSS::CreateVP9FrameBuilder();
                    break;
#if defined(_WIN32)
                case Video::fccWXWL::ID:
                    m_currentFrameBuilder = NMMSS::CreateWavecamFrameBuilder();
                    break;
#endif
                case Video::fccVendor::ID:
                    m_currentFrameBuilder = NMMSS::CreateNullFrameBuilder();
                    break;
                default:
                    m_currentFrameBuilder = NMMSS::CreateNullFrameBuilder();
                    break;
                }
            }
            break;
            default:
                m_currentFrameBuilder = NMMSS::CreateNullFrameBuilder();
                break;
            }

            // to delete old data for current majorID, if there are in m_multimediaSpecificData
            const auto multimediaSpecificDataItem = std::find_if(m_multimediaSpecificData.begin(), m_multimediaSpecificData.end(),
                [this](MultimediaSpecificData_t::value_type value)
                {
                    return value.second.m_descriptor->GetMajorID() == m_currentDescriptor->GetMajorID();
                });
            if (multimediaSpecificDataItem != m_multimediaSpecificData.end())
                m_multimediaSpecificData.erase(multimediaSpecificDataItem);

            m_multimediaSpecificData[name].m_descriptor = m_currentDescriptor;
            m_multimediaSpecificData[name].m_frameBuilder = m_currentFrameBuilder;

            _inf_ << m_logPrefix << " FrameFactory initialized by media format: " <<
                m_currentDescriptor->ToString() << std::endl;
            m_currentFrameBuilder->Restart();
        }
    }

    try
    {
        return createFrameFunction( sample.Get(), m_currentDescriptor->GetSubtype(),
            m_currentDescriptor->GetVendor(), m_currentDescriptor->GetCodec(), m_currentFrameBuilder.Get() );
    }
    catch (const std::exception& e)
    {
        _err_ << m_logPrefix << " Error on create " << typeid(R).name() << ":" << e.what() << std::endl;
    }
    catch (...)
    {
        _err_ << m_logPrefix << " Error on create " << typeid(R).name() << "." << std::endl;
        throw;
    }

    return 0;
}

ICompressedBuffer * CFrameFactory::AllocateCompressedFrame(char const* name,
    ITV8::uint32_t bufferSize, ITV8::timestamp_t timestamp, ITV8::bool_t isKeyFrame)
{
    if (m_lastName != name && m_multimediaSpecificData.find(name) == m_multimediaSpecificData.end())
    {
        _log_ << m_logPrefix << " AllocateCompressedFrame:"<<
            "name:" <<name<<", " <<
            "bufferSize:" <<bufferSize<<", " <<
            "timestamp:" <<timestamp<<", " <<
            "isKeyFrame:" <<isKeyFrame<<");"<<std::endl;
    }
    return CreateTypedFrame<ICompressedBuffer, Video>(name, bufferSize,
        [=](NMMSS::ISample* sample, ::uint32_t subtype,::uint16_t vendor, ::uint16_t codec, NMMSS::IFrameBuilder* frameBuilder)
        {
            return new CCompressedBuffer(frameBuilder, sample, subtype, vendor, codec, name, bufferSize, timestamp, isKeyFrame);
        });
}

ICompositeBuffer * CFrameFactory::AllocateCompositeFrame(char const* name,
    ITV8::uint32_t bufferSize, ITV8::int32_t stride, ITV8::timestamp_t timestamp,
    ITV8::uint32_t width, ITV8::uint32_t height)
{
    if (m_lastName != name && m_multimediaSpecificData.find(name) == m_multimediaSpecificData.end())
    {
        _log_ << m_logPrefix << " AllocateCompositeFrame:"<<
            "name:" <<name<<", " <<
            "bufferSize:" <<bufferSize<<", " <<
            "stride:" <<stride<<", " <<
            "timestamp:" <<timestamp<<", " <<
            "width:" <<width<<", " <<
            "height:" <<height<<");"<<std::endl;
    }
    
    return CreateTypedFrame<ICompositeBuffer, Video>(name, bufferSize,
        [=](NMMSS::ISample* sample, ::uint32_t minorID, ::uint16_t, ::uint16_t, NMMSS::IFrameBuilder*)
    {
        return new CCompositeBuffer(sample, minorID, name, bufferSize, stride, GetNormalizedTimestamp(timestamp), width, height);
    });
}

IPlanarBuffer * CFrameFactory::AllocatePlanarFrame(char const * name,
    ITV8::uint32_t sizeY, ITV8::uint32_t sizeU, ITV8::uint32_t sizeV,
    ITV8::uint32_t strideY, ITV8::uint32_t strideU, ITV8::uint32_t strideV,
    ITV8::timestamp_t timestamp, ITV8::uint32_t width, ITV8::uint32_t height)
{
    if (m_lastName != name && m_multimediaSpecificData.find(name) == m_multimediaSpecificData.end())
    {
        _log_ << m_logPrefix << " AllocatePlanarFrame("<<
            "name:" <<name<<", " <<
            "sizeY:" <<sizeY<<", " <<
            "sizeU:" <<sizeU<<", " <<
            "sizeV:" <<sizeV<<", " <<
            "strideY:" <<strideY<<", " <<
            "strideU:" <<strideU<<", " <<
            "strideV:" <<strideV<<", " <<
            "timestamp:" <<timestamp<<", " <<
            "width:" <<width<<", " <<
            "height:" <<height<<");"<<std::endl;
    }
    // Вынуждены упаковать праметры в структуру, т.к. boost::lambda::new_ptr поддерживает только
    // 9 аргументов для конструктора.
    CPlanarBuffer::SArgs args = {sizeY, sizeU, sizeV, strideY, strideU, strideV};

    ::uint32_t bufferSize = sizeU + sizeV + sizeY;
    // ДЛЯ CPlanarBuffer НЕ МЕНЯЕМ GetNormalizedTimestamp(timestamp), иначе при распаковки
    // испортим реальное время.
    // TODO: ВНИМАНИЕ!!! Надо править работу с временем от IP камер.
    // Как вариант, всегда воспринимать время приходящее в AllocateXXXFrame количество
    // миллисеунд  от 1970 года по UTC/GMT.
    return CreateTypedFrame<IPlanarBuffer, Video>(name, bufferSize,
        [=](NMMSS::ISample* sample, ::uint32_t minorID, ::uint16_t, ::uint16_t, NMMSS::IFrameBuilder*)
        {
            return new CPlanarBuffer(GET_LOGGER_PTR, sample, minorID, name, args, timestamp, width, height);
        });
}

IAudioBuffer* CFrameFactory::AllocateAudioCompressedFrame(
    const char* name, ITV8::uint32_t bufferSize, ITV8::timestamp_t timestamp)
{
    if (m_lastName != name && m_multimediaSpecificData.find(name) == m_multimediaSpecificData.end())
    {
        _log_ << m_logPrefix << " AllocateAudioCompressedFrame("<<
            "name:" <<name<<", " <<
            "bufferSize:" <<bufferSize<<", " <<
            "timestamp:" <<timestamp<<");"<<std::endl;
    }
    return CreateTypedFrame<IAudioBuffer, Audio>(name, bufferSize,
        [=](NMMSS::ISample* sample, ::uint32_t minorID, ::uint16_t, ::uint16_t, NMMSS::IFrameBuilder*)
        {
            return new CAudioBuffer(sample, minorID, name, bufferSize,
                GetNormalizedTimestamp(timestamp));
        });
}

IAudioBufferPcm* CFrameFactory::AllocateAudioPcmFrame(
    const char* name, ITV8::uint32_t bufferSize, ITV8::timestamp_t timestamp,
    ITV8::uint32_t sampleRate, ITV8::uint32_t bitPerSample, ITV8::uint32_t channels)
{
    if (m_lastName != name && m_multimediaSpecificData.find(name) == m_multimediaSpecificData.end())
    {
        _log_ << m_logPrefix << " AllocateAudioPcmFrame("<<
            "name:" <<name<<", " <<
            "bufferSize:" <<bufferSize<<", " <<
            "timestamp:" <<timestamp<<", " <<
            "sampleRate:" <<sampleRate<<", " <<
            "bitPerSample:" <<bitPerSample<<", " <<
            "channels:" <<channels<<");"<<std::endl;
    }
    return CreateTypedFrame<IAudioBufferPcm, Audio>(name, bufferSize,
        [=](NMMSS::ISample* sample, ::uint32_t minorID, ::uint16_t, ::uint16_t, NMMSS::IFrameBuilder*)
    {
        return new CAudioPcmBuffer(sample, minorID, name, bufferSize,
            GetNormalizedTimestamp(timestamp), sampleRate, bitPerSample, channels);
    });
}

ITV8::MFF::IAudioBufferG7xx* CFrameFactory::AllocateAudioG7xxFrame(
    const char* name, ITV8::uint32_t bufferSize, ITV8::timestamp_t timestamp,
    ITV8::uint32_t bitRate, ITV8::uint32_t encoding)
{
    if (m_lastName != name && m_multimediaSpecificData.find(name) == m_multimediaSpecificData.end())
    {
        _log_ << m_logPrefix << " AllocateAudioG7xxFrame("<<
            "name:" <<name<<", " <<
            "bufferSize:" <<bufferSize<<", " <<
            "timestamp:" <<timestamp<<", " <<
            "bitRate:" <<bitRate<<", " <<
            "encoding:" <<encoding<<");"<<std::endl;
    }
    return CreateTypedFrame<IAudioBufferG7xx, Audio>(name, bufferSize,
        [=](NMMSS::ISample* sample, ::uint32_t minorID, ::uint16_t, ::uint16_t, NMMSS::IFrameBuilder*)
    {
        return new CAudioG7xxBuffer(sample, minorID, name, bufferSize,
            GetNormalizedTimestamp(timestamp), bitRate, encoding);
    });
}

ITV8::MFF::IAudioBufferG7xxEx* CFrameFactory::AllocateAudioG7xxExFrame(const char* szName,
    ITV8::uint32_t nBufferSize, ITV8::timestamp_t timestamp, ITV8::uint32_t bitRate,
    ITV8::uint32_t encoding, ITV8::uint32_t	channelsNum, ITV8::uint32_t sampleRate,
    ITV8::uint32_t bitsPerSample)
{
    if (m_lastName != szName && m_multimediaSpecificData.find(szName) == m_multimediaSpecificData.end())
    {
        _log_ << m_logPrefix << " AllocateAudioG7xxExFrame(" <<
            "name:" << szName << ", " <<
            "bufferSize:" << nBufferSize << ", " <<
            "timestamp:" << timestamp << ", " <<
            "bitRate:" << bitRate << ", " <<
            "sampleRate:" << sampleRate << ", " <<
            "bitsPerSample:" << bitsPerSample << ", " <<
            "channelsNum:" << channelsNum << ", " <<
            "encoding:" << encoding << ");" << std::endl;
    }
    return CreateTypedFrame<IAudioBufferG7xxEx, Audio>(szName, nBufferSize,
        [=](NMMSS::ISample* sample, ::uint32_t minorID, ::uint16_t, ::uint16_t, NMMSS::IFrameBuilder*)
        {
            return new CAudioG7xxBuffer(sample, minorID, szName, nBufferSize,
                GetNormalizedTimestamp(timestamp), bitRate, encoding,
                channelsNum, sampleRate, bitsPerSample);
        });
}

ITV8::timestamp_t CFrameFactory::GetNormalizedTimestamp(ITV8::timestamp_t timestamp)
{
    if(!timestamp && !m_invalidTimestampLogged)
    {
        _err_ << m_logPrefix << " AllocateCompressedFrame(...) - Invalid argument:"
            " \"timestamp\" must be not 0. This error will be ignored for other frames." << std::endl;
        m_invalidTimestampLogged = true;
    }

    //IPIN-2744 workaround
    if (0 == timestamp)
    {
        boost::posix_time::ptime dtNow=boost::posix_time::microsec_clock::universal_time();
        timestamp = ITV8::PtimeToTimestamp(dtNow);
    }
    return timestamp;
}

ITV8::MFF::ICompressedBuffer2*  CFrameFactory::AllocateCompressedFrame(const char* szName,
    ITV8::uint32_t bufferSize, ITV8::timestamp_t timestamp, ITV8::uint8_t frameType)
{
    _err_ << m_logPrefix << " AllocateCompressedFrame(...) not implemented." << std::endl;
    m_invalidTimestampLogged = true;
        //TODO: implement it.
        return 0;
}

ITV8::MFF::IAudioBufferMPEG*    CFrameFactory::AllocateAudioMPEGFrame(const char* name,
    ITV8::uint32_t bufferSize, ITV8::timestamp_t timestamp, ITV8::uint32_t nExtraSize)
{
    if (m_lastName != name && m_multimediaSpecificData.find(name) == m_multimediaSpecificData.end())
    {
        _log_ << m_logPrefix << " AllocateAudioMPEGFrame("<<
            "name:" <<name<<", " <<
            "bufferSize:" <<bufferSize<<", " <<
            "timestamp:" <<timestamp<<", " <<
            "nExtraSize:" <<nExtraSize<<");"<<std::endl;
    }

    const ITV8::uint32_t extraDataSize = nExtraSize ? nExtraSize : GetExtraDataSize();

    const ITV8::uint32_t fullBufferSize = bufferSize + extraDataSize;
    return CreateTypedFrame<IAudioBufferMPEG, Audio>(name, fullBufferSize,
        [=](NMMSS::ISample* sample, ::uint32_t minorID, ::uint16_t, ::uint16_t, NMMSS::IFrameBuilder*)
    {
        return new CAudioBufferMpeg(m_allocator.Get(), sample, minorID, name, fullBufferSize, GetNormalizedTimestamp(timestamp), nExtraSize, this);
    });
}

ITV8::Analytics::ITargetEnumeratorAdjuster* CFrameFactory::AllocateTargetEnumeratorAdjuster(
    ITV8::timestamp_t timestamp)
{
    return new ITV8::Framework::TargetEnumeratorAdjuster(timestamp);
}

const ITV8::uint8_t* CFrameFactory::GetExtraData() const
{
    return m_extraData.data();
}

size_t CFrameFactory::GetExtraDataSize() const
{
    return m_extraData.size();
}

void CFrameFactory::SetExtraData(const ITV8::uint8_t* extraData, size_t extraDataSize)
{
    m_extraData.clear();
    std::copy(extraData, extraData + extraDataSize, std::back_inserter(m_extraData));
}
