#ifndef ITVSDKUTIL_IMEDIAFORMATDICTIONARY_H
#define ITVSDKUTIL_IMEDIAFORMATDICTIONARY_H

#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include "../mIntTypes.h"

namespace NMMSS
{
    class ISample;
}

namespace ITVSDKUTILES
{

class IMediaFormatDescriptor : boost::noncopyable
{
public:
    virtual ~IMediaFormatDescriptor() { }

    virtual bool IsValid() const = 0;

    // ¬озвращает числовой идентификатор семейства форматов (аудио, видео и пр.) прин€тый в Ngp. 
    // —м. mmss\MediaType.h
    virtual ::uint32_t GetMajorID() const = 0;

    // ¬озвращает числовой идентификатор формата (аудио, видео и пр.) прин€тый в Ngp. 
    virtual ::uint32_t GetSubtype() const = 0;

    // ¬озвращает истину, если формат задан при помощи вендора и кодека.
    virtual bool HasVendor() const = 0;

    // ¬озвращает идентификатор дл€ поставщека кодека, которые описаны в 
    // ItvMediaSdk\include\codecConstants.h. ƒл€ кодеков разрабатываемых Itv GetVendor возвращает 0.
    virtual ::uint16_t GetVendor() const = 0;

    // ¬озвращает уникальный дл€ вендора идентификатор кодека, которые описаны в 
    // ItvMediaSdk\include\codecConstants.h.
    virtual ::uint16_t GetCodec() const = 0;

    // ¬озвращает истину, если дл€ формата есть строковое представление
    virtual bool HasName() const =0;

    // возвращает строковое представление формата, прин€тое в ITV Codec Pack
    virtual const char *GetName() const = 0;

    // ¬озвращает текстовое описание формата.
    virtual const char* ToString() const = 0;
};

typedef boost::shared_ptr<const IMediaFormatDescriptor> IMediaFormatDescriptorPtr;

class IMediaFormatDictionary : boost::noncopyable
{
public:
    virtual IMediaFormatDescriptorPtr Find(const char* name) const = 0;
    virtual IMediaFormatDescriptorPtr Find(NMMSS::ISample const& sample) const = 0;
};

}
#endif //ITVSDKUTIL_IMEDIAFORMATDICTIONARY_H
