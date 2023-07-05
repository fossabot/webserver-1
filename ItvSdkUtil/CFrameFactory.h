#ifndef ITVSDKUTIL_CFRAMEFACTORY_H
#define ITVSDKUTIL_CFRAMEFACTORY_H

#include "ItvSdkUtil.h"
#include "../Sample.h"
#include <Logging/log2.h>
#include <ItvMediaSdk/include/frameFactorySdk.h>
#include <ItvDetectorSdk/include/ItvDetectorsSdk2.h>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <map>

#ifdef _MSC_VER
#pragma warning(push)
//class 'boost::shared_ptr<T>' needs to have dll-interface to be used by
//TODO: после того как исчезнет необходимость в ITVSDKUTILES_API удалить.
#pragma warning(disable: 4251)
#pragma warning(disable: 4275)
#endif

class CFormatLib;
namespace NMMSS 
{ 
	class IAllocator; 
	class IFrameBuilder;
}

// Used to hold last known extra data for AAC to include it to each sample when detaching it.
struct IExtraDataHolder
{
    virtual const ITV8::uint8_t* GetExtraData() const = 0;
    virtual size_t GetExtraDataSize() const = 0;
    virtual void SetExtraData(const ITV8::uint8_t* extraData, size_t extraDataSize) = 0;

protected:
    ~IExtraDataHolder() {}
};

//    Attention! This class is NOT thread safe and don't support async operations
//    because must support GetLastAllocSample

class ITVSDKUTILES_API CFrameFactory : 
	private boost::noncopyable, 
    public ITV8::Analytics::ITargetEnumeratorFactory,
    public ITV8::MFF::IMultimediaFrameFactory4,
    public IExtraDataHolder
{
//ITV8::IContract implementation
public:
    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY2(ITV8::IContract, ITV8::Analytics::ITargetEnumeratorFactory)
        ITV8_CONTRACT_ENTRY2(ITV8::MFF::IMultimediaFrameFactory, ITV8::Analytics::ITargetEnumeratorFactory)
        ITV8_CONTRACT_ENTRY(ITV8::MFF::IMultimediaFrameFactory2)
        ITV8_CONTRACT_ENTRY(ITV8::MFF::IMultimediaFrameFactory4)
		ITV8_CONTRACT_ENTRY(ITV8::Analytics::ITargetEnumeratorFactory)
    ITV8_END_CONTRACT_MAP()

public:

    explicit CFrameFactory(DECLARE_LOGGER_ARG, NMMSS::IAllocator* allocator, const char* name);
    virtual ~CFrameFactory();

    void SetAllocator(NMMSS::IAllocator* allocator);

// ITV8::MFF::IMultimediaFrameFactory implementation
public:
    virtual ITV8::MFF::ICompressedBuffer* AllocateCompressedFrame(char const* name,
        ITV8::uint32_t bufferSize, ITV8::timestamp_t timestamp, ITV8::bool_t isKeyFrame);

    virtual ITV8::MFF::ICompositeBuffer* AllocateCompositeFrame(char const *,
        ITV8::uint32_t bufferSize, ITV8::int32_t stride, ITV8::timestamp_t,
        ITV8::uint32_t width, ITV8::uint32_t height);

    virtual ITV8::MFF::IPlanarBuffer* AllocatePlanarFrame(char const *,
        ITV8::uint32_t sizeY, ITV8::uint32_t sizeU, ITV8::uint32_t sizeV,
        ITV8::uint32_t strideY, ITV8::uint32_t strideU, ITV8::uint32_t strideV,
        ITV8::timestamp_t, ITV8::uint32_t width, ITV8::uint32_t height);

    virtual ITV8::MFF::IAudioBuffer* AllocateAudioCompressedFrame(const char* szName,
        ITV8::uint32_t nBufferSize, ITV8::timestamp_t timestamp);

    virtual ITV8::MFF::IAudioBufferPcm*	AllocateAudioPcmFrame(const char* szName,
        ITV8::uint32_t nBufferSize, ITV8::timestamp_t timestamp, ITV8::uint32_t sampleRate,
        ITV8::uint32_t bitPerSample, ITV8::uint32_t channels);

    virtual ITV8::MFF::IAudioBufferG7xx* AllocateAudioG7xxFrame(const char* szName,
        ITV8::uint32_t nBufferSize, ITV8::timestamp_t timestamp, ITV8::uint32_t bitRate, 
        ITV8::uint32_t encoding);

// ITV8::MFF::IMultimediaFrameFactory2 implementation
public:
	virtual ITV8::MFF::ICompressedBuffer2*	AllocateCompressedFrame(const char* szName,
		ITV8::uint32_t nBufferSize, ITV8::timestamp_t timestamp, ITV8::uint8_t frameType);

	virtual ITV8::MFF::IAudioBufferMPEG*	AllocateAudioMPEGFrame(const char* szName,
		ITV8::uint32_t nBufferSize, ITV8::timestamp_t timestamp, ITV8::uint32_t nExtraSize);

// ITV8::MFF::IMultimediaFrameFactory3 implementation
public:
    virtual ITV8::MFF::IAudioBufferEx* AllocateAudioExFrame(const char* szName,
        ITV8::uint32_t nBufferSize, ITV8::timestamp_t timestamp, ITV8::uint32_t sampleRate,
        ITV8::uint32_t bitPerSample, ITV8::uint32_t channels,
        ITV8::uint32_t blockAlign, ITV8::uint32_t avgBytesPerSec) 
    {
        return nullptr; 
    }

// ITV8::MFF::IMultimediaFrameFactory4 implementation
    virtual ITV8::MFF::IAudioBufferG7xxEx* AllocateAudioG7xxExFrame(const char* szName,
        ITV8::uint32_t nBufferSize, ITV8::timestamp_t timestamp, ITV8::uint32_t bitRate,
        ITV8::uint32_t encoding, ITV8::uint32_t	channelsNum, ITV8::uint32_t sampleRate,
        ITV8::uint32_t bitsPerSample);

// ITV8::Analytics::ITargetEnumeratorFactory implementation
public:
    // Creates a new instance of ITargetEnumerator
    // Parameters:
    //  timestamp - specifies a time of frame which contains targets.
    virtual ITV8::Analytics::ITargetEnumeratorAdjuster* AllocateTargetEnumeratorAdjuster(
        ITV8::timestamp_t timestamp);

private:
    // IExtraDataHolder implementation.
    virtual const ITV8::uint8_t* GetExtraData() const;
    virtual size_t GetExtraDataSize() const;
    virtual void SetExtraData(const ITV8::uint8_t* extraData, size_t extraDataSize);

private:
    inline ITV8::timestamp_t GetNormalizedTimestamp(ITV8::timestamp_t timestamp);

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
    R *CreateTypedFrame(char const* name, ITV8::uint32_t bufferSize, 
        TCreateFrameFunction createFrameFunction);

protected:

    struct FrameBuilderAndMediaFormatDescriptor
    {
        NCorbaHelpers::CAutoPtr<NMMSS::IFrameBuilder> m_frameBuilder;
        ITVSDKUTILES::IMediaFormatDescriptorPtr m_descriptor;
    };

    DECLARE_LOGGER_HOLDER;

    // Specifies the name of factory instance to distinguish one factory from the other in log.
    std::string m_logPrefix;

    NCorbaHelpers::CAutoPtr<NMMSS::IAllocator> m_allocator;

    NCorbaHelpers::CAutoPtr<NMMSS::IFrameBuilder> m_currentFrameBuilder;
    std::string m_lastName;
    ITVSDKUTILES::IMediaFormatDescriptorPtr m_currentDescriptor;

    typedef std::map<std::string, FrameBuilderAndMediaFormatDescriptor> MultimediaSpecificData_t;
    MultimediaSpecificData_t m_multimediaSpecificData;

private:
    // Indicates whether message about empty timestamp was printed to log.
    bool m_invalidTimestampLogged;

    std::vector<ITV8::uint8_t> m_extraData;
};


ITVSDKUTILES_API
ITV8::MFF::IMultimediaBuffer* CreateFrameFromSample(DECLARE_LOGGER_ARG, 
    NMMSS::ISample const* sample,
    char const* name);

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif // ITVSDKUTIL_CFRAMEFACTORY_H
