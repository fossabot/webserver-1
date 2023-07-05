#include "MediaFormatDictionary.h"
#include <ItvMediaSdk/include/codecConstants.h>
#include "ItvSdkUtil.h"
#include "../MediaType.h"

#include <vector>
#include <sstream>
#include <limits>
#include <mutex>
#include <boost/make_shared.hpp>
#include <boost/lambda/lambda.hpp>
#include <boost/lambda/bind.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/lexical_cast.hpp>

using namespace ITVSDKUTILES;

namespace
{

class CDescriptor : public IMediaFormatDescriptor
{
public:
    typedef boost::shared_ptr<const CDescriptor> Ptr;

    CDescriptor()
        :m_major(std::numeric_limits< ::uint32_t>::max()),
        m_subtype(std::numeric_limits< ::uint32_t>::max()),
        m_vendor(std::numeric_limits< ::uint16_t>::max()),
        m_codec(std::numeric_limits< ::uint16_t>::max())
    {
        MakeDescriptor();
    }

    // Creates a new instance of CDescriptor with specified major,subtype and name.
    CDescriptor(::uint32_t major, ::uint32_t subtype, const std::string& name)
        :m_major(major), m_subtype(subtype),
        m_vendor(std::numeric_limits< ::uint16_t>::max()),
        m_codec(std::numeric_limits< ::uint16_t>::max()), m_name(name)
    {
        MakeDescriptor();
    }

    // Creates a new instance of CDescriptor with specified major, subtype, vendor, codec and
    // name.
    CDescriptor(::uint32_t major, ::uint32_t subtype, ::uint16_t vendor, ::uint16_t codec,
        const std::string& name)
        :m_major(major), m_subtype(subtype), m_vendor(vendor), m_codec(codec), m_name(name)
    {
        MakeDescriptor();
    }

// ITVSDKUTILES::IMediaFormatDescriptor implementation
public:

    virtual bool IsValid() const
    {
        return m_major != std::numeric_limits< ::uint32_t>::max() &&
            m_subtype != std::numeric_limits< ::uint32_t>::max();
    }

    // Возвращает числовой идентификатор семейства форматов (аудио, видео и пр.) принятый в Ngp.
    // См. mmss\MediaType.h
    virtual ::uint32_t GetMajorID() const
    {
        return m_major;
    }

    // Возвращает числовой идентификатор формата (аудио, видео и пр.) принятый в Ngp.
    virtual ::uint32_t GetSubtype() const
    {
        return m_subtype;
    }

    // Возвращает истину, если формат задан при помощи вендора и кодека.
    virtual bool HasVendor() const
    {
        using namespace NMMSS::NMediaType;
        return m_major ==Video::ID && m_subtype == Video::fccVendor::ID;
    }

    // Возвращает идентификатор для поставщека кодека, которые описаны в
    // ItvMediaSdk\include\codecConstants.h. Для кодеков разрабатываемых Itv GetVendor возвращает 0.
    virtual ::uint16_t GetVendor() const
    {
        return m_vendor;
    }

    // Возвращает уникальный для вендора идентификатор кодека, которые описаны в
    // ItvMediaSdk\include\codecConstants.h.
    virtual ::uint16_t GetCodec() const
    {
        return m_codec;
    }

    // Возвращает истину, если для формата есть строковое представление
    virtual bool HasName() const
    {
        return m_name.size() != 0;

    }

    // возвращает строковое представление формата, принятое в ITV Codec Pack
    virtual const char *GetName() const
    {
        return m_name.c_str();
    }

    virtual const char* ToString() const
    {
        return m_descriptor.c_str();
    }


public:
    const ::uint32_t m_major;

    const ::uint32_t m_subtype;

    // Идентификатор для вендора
    const ::uint16_t m_vendor;

    // Идентификатор для кодека
    const ::uint16_t m_codec;

    const std::string m_name;

private:
    void MakeDescriptor()
    {
        std::ostringstream ss;
        ss << m_major << ":" << std::string(reinterpret_cast<const char*>(&m_subtype),4) << ":"
            << m_vendor << ":" << m_codec << "-" << (m_name.size() ? m_name : "NgpEmbedded");
        m_descriptor = ss.str();
    }

private:
    std::string m_descriptor;
};

inline std::string GenerateVendorName(::uint16_t vendor, ::uint16_t codec)
{
    std::ostringstream ss;
    ss << ITV_VNDR_CODEC_PREFIX << "." << vendor << "." << codec;
    return ss.str();
}


inline CDescriptor* MakeDescriptor(const std::string& name)
{
    using namespace NMMSS::NMediaType;

    std::vector<std::string> parts;
    boost::split(parts, name, boost::is_any_of("."), boost::token_compress_on);

    if(parts.size() < 3 || parts[0] != ITV_VNDR_CODEC_PREFIX)
        throw std::runtime_error("Can't make IMediaFormatDescriptor: name \"" +name+"\" has invalid format.");

    return new CDescriptor(Video::ID, Video::fccVendor::ID,
        boost::lexical_cast< ::uint16_t>(parts[1]), boost::lexical_cast< ::uint16_t>(parts[2]), name);
}

class CMediaFormatDictionary : public IMediaFormatDictionary
{
    DECLARE_LOGGER_HOLDER;
private:

    struct byNgpID{};

    struct byName{};

public:
    CMediaFormatDictionary(DECLARE_LOGGER_ARG)
    {
        INIT_LOGGER_HOLDER;

        using namespace NMMSS::NMediaType;
        using namespace ITV8::CSDK;

        Register<Video::fccJPEG>(ITV8_VIDEO_CODEC_MJPEG);
        Register<Video::fccJPEG2000>(ITV8_VIDEO_CODEC_JP2K);
        Register<Video::fccMXPEG>(ITV8_VIDEO_CODEC_MXPEG);
        Register<Video::fccMPEG4>(ITV8_VIDEO_CODEC_MPEG4);
        Register<Video::fccMPEG2>(ITV8_VIDEO_CODEC_MPEG2);
        Register<Video::fccH264>(ITV8_VIDEO_CODEC_H264);
        Register<Video::fccH264SVC>(ITV8_VIDEO_CODEC_H264SVC);
        Register<Video::fccH264SVCT>(ITV8_VIDEO_CODEC_H264SVCT);
        Register<Video::fccH265>(ITV8_VIDEO_CODEC_H265);
        Register<Video::fccVP8>(ITV8_VIDEO_CODEC_VP8);
        Register<Video::fccVP9>(ITV8_VIDEO_CODEC_VP9);
#ifdef _WIN32
        Register<Video::fccWXWL>(ITV8_VIDEO_CODEC_WXWL);
#endif
        Register<Video::fccVendor>(ITV8_VIDEO_CODEC_H264_INTERLACED, AXXON_SOFT, ASC_H264_INTERLACED);
        Register<Video::fccVendor>(ITV8_VIDEO_CODEC_H263,            AXXON_SOFT, ASC_H263);
        Register<Video::fccVendor>(ITV8_VIDEO_CODEC_VWV,             AXXON_SOFT, ASC_VWV);
        Register<Video::fccVendor>(ITV8_VIDEO_CODEC_HIKVISION,       AXXON_SOFT, ASC_H264_HIKVISION);

        Register<Video::fccI420>("YUV420");
        Register<Video::fccY42B>("YUV422");
        Register<Video::fccGREY>("GREY");

        Register<Audio::G711>(ITV8_AUDIO_CODEC_G711);
        Register<Audio::G726>(ITV8_AUDIO_CODEC_G726);
        Register<Audio::AAC>(ITV8_AUDIO_CODEC_AAC);
        Register<Audio::PCM>(ITV8_AUDIO_CODEC_PCM);
        Register<Audio::MP2>(ITV8_AUDIO_CODEC_MP2);

        this->ngp_Logger_Ptr_ = nullptr;
    }

    virtual IMediaFormatDescriptorPtr Find(NMMSS::ISample const& sample) const
    {
        using namespace NMMSS::NMediaType;

        const NMMSS::SMediaSampleHeader& header = const_cast<NMMSS::ISample&>(sample).Header();
        bool hasVendor = header.nMajor == Video::ID && header.nSubtype == Video::fccVendor::ID;
        ::uint16_t vendor = std::numeric_limits< ::uint16_t>::max();
        ::uint16_t codec = std::numeric_limits< ::uint16_t>::max();

        // If it's video sample of optional vendor.
        if(hasVendor)
        {
            //TODO: Здесь нет поддержки семплов NMMSS::NMediaType::Audio::fccVendor.
            Video::fccVendor::SubtypeHeader const& subHeader = sample.SubHeader<Video::fccVendor>();

            vendor = subHeader.nVendor;
            codec = subHeader.nCodec;
        }

        TDescriptors::index<byNgpID>::type::const_iterator it =
            m_descriptors.get<byNgpID>().find(boost::make_tuple(header.nMajor,
            header.nSubtype, vendor, codec));

        if (it != m_descriptors.end())
        {
            return *it;
        }

        std::string name = hasVendor ? GenerateVendorName(vendor, codec) : std::string();
        return IMediaFormatDescriptorPtr(new CDescriptor(header.nMajor, header.nSubtype,
            vendor, codec, name));
    }

    virtual IMediaFormatDescriptorPtr Find(const char* name) const
    {
        std::string sname(name ? name : "");
        TDescriptors::index<byName>::type::const_iterator it =
            m_descriptors.get<byName>().find(sname);

        if (it != m_descriptors.get<byName>().end())
        {
            return *it;
        }
        // If it's video sample of optional vendor.
        else if(sname.find(ITV_VNDR_CODEC_PREFIX)!=std::string::npos)
        {
            //TODO: Здесь нет поддержки семплов NMMSS::NMediaType::Audio::fccVendor.
            return CDescriptor::Ptr(MakeDescriptor(sname));
        }

        return GetNull();
    }

private:
    static IMediaFormatDescriptorPtr GetNull()
    {
        static IMediaFormatDescriptorPtr value(new CDescriptor());
        return value;
    }

    template<class TMediaType>
    void Register(const std::string& formatName,
        ::uint16_t vendor=std::numeric_limits< ::uint16_t>::max(),
        ::uint16_t codec = std::numeric_limits< ::uint16_t>::max())
    {
        CDescriptor::Ptr descriptor(new CDescriptor(TMediaType::Major::ID, TMediaType::ID,
            vendor, codec, formatName));

        if( !m_descriptors.insert(descriptor).second )
        {
            throw std::runtime_error(std::string("CMediaFormatDictionary::Register(")
                +descriptor->ToString()+") failed. The key already exists.");
        }

        _inf_ << "CMediaFormatDictionary::Register("<< descriptor->ToString() <<")" << std::endl;
    }

    // see: http://www.solarix.ru/for_developers/cpp/boost/multi_index/ru/an/multi_index.shtml
    typedef boost::multi_index_container<
        CDescriptor::Ptr,
        boost::multi_index::indexed_by<
            // Описываем индекс в котором ключом являются major и subtype Ngp семпла
            boost::multi_index::ordered_unique<
                boost::multi_index::tag<byNgpID>,
                boost::multi_index::composite_key<
                    CDescriptor::Ptr,
                    boost::multi_index::member<CDescriptor, const ::uint32_t, &CDescriptor::m_major>,
                    boost::multi_index::member<CDescriptor, const ::uint32_t, &CDescriptor::m_subtype>,
                    boost::multi_index::member<CDescriptor, const ::uint16_t, &CDescriptor::m_vendor>,
                    boost::multi_index::member<CDescriptor, const ::uint16_t, &CDescriptor::m_codec>
                >
            >,
            // Описываем индекс в котором ключом являются имя кодека принятое в Itv
            boost::multi_index::ordered_unique<
                boost::multi_index::tag<byName>,
                boost::multi_index::member<CDescriptor, const std::string, &CDescriptor::m_name>
            >
        >
    > TDescriptors;
    TDescriptors m_descriptors;
};

}

namespace
{
    std::once_flag MediaFormatDictionaryFlag;
    std::unique_ptr<CMediaFormatDictionary> MediaFormatDictionary;
}

namespace ITVSDKUTILES
{
    ITVSDKUTILES_API const IMediaFormatDictionary& GetMediaFormatDictionary(DECLARE_LOGGER_ARG)
    {
        std::call_once(MediaFormatDictionaryFlag, [&] { MediaFormatDictionary.reset(new CMediaFormatDictionary(GET_LOGGER_PTR)); });
        return *MediaFormatDictionary;
    }
}
