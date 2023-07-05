#ifndef IPUTIL_STORAGEDATATYPES_H
#define IPUTIL_STORAGEDATATYPES_H

#include <boost/shared_ptr.hpp>
#include <string>
#include <vector>

#include <ItvDeviceSdk/include/IStorageDevice.h>
#include <ItvDeviceSdk/include/IRecordingSearch.h>
#include <IpUtil/include/Safe.h>

namespace ITV8
{
using GDRV::DateTimeRange;

namespace Utility
{

namespace Storage = GDRV::Storage;

namespace detail
{

template<typename T>
struct EnumerateTraits
{
    typedef T* result_type;

    static result_type get(T& val) { return &val; }
};

template<typename T>
struct EnumerateTraits<boost::shared_ptr<T>>
{
    typedef T* result_type;

    static result_type get(const boost::shared_ptr<T>& val) { return val.get(); }
};

template<>
struct EnumerateTraits<std::string>
{
    typedef const char* result_type;

    static result_type get(const std::string& val) { return val.c_str(); }
};

}

/// General implementation for IPINT interface enumerator.
template <typename TEnumeratorInterface, typename TElementContainer>
class BasicEnumeratorImpl : public TEnumeratorInterface
{
public:
    explicit BasicEnumeratorImpl(TElementContainer& container) :
        m_container(container),
        m_it(container.rend())
    {}

    virtual bool MoveNext()
    {
        if (m_it == m_container.rbegin())
        {
            return false;
        }
        --m_it;
        return true;
    }

    virtual typename detail::EnumerateTraits<typename TElementContainer::value_type>::result_type GetCurrent()
    {
        if (m_it == m_container.rend())
        {
            return 0;
        }
        return detail::EnumerateTraits<typename TElementContainer::value_type>::get(*m_it);
    }

    virtual void Reset() { m_it = m_container.rend(); }

    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY(TEnumeratorInterface)
        ITV8_CONTRACT_ENTRY(IContract)
    ITV8_END_CONTRACT_MAP()

private:
    typedef typename TElementContainer::reverse_iterator iterator_t;
    TElementContainer& m_container;
    iterator_t m_it;
};

/// Implementation of ITV8::GDRV::Storage::ITrackInfo interface.
struct TrackInfo : public Storage::ITrackInfo
{
    TrackInfo(const std::string& trackId, const std::string& trackDescr, 
            Storage::TTrackMediaType mediaType) :
        id(trackId),
        description(trackDescr),
        type(mediaType)
    {}

    explicit TrackInfo(const Storage::ITrackInfo* info) :
        id(safeCopyString(info->GetId())),
        description(safeCopyString(info->GetDescription())),
        type(info->GetMediaType())
    {}

    virtual const char* GetId() const { return id.c_str(); }
    virtual const char* GetDescription() const { return description.c_str(); }
    virtual ITV8::uint32_t GetMediaType() const { return type; }

    std::string        id;
    std::string        description;
    int                type;

    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY(Storage::ITrackInfo)
        ITV8_CONTRACT_ENTRY(IContract)
    ITV8_END_CONTRACT_MAP()
};

typedef std::vector<TrackInfo> tracksInfoList_t;

/// Pushes tracks info from IPINT interface enumerator to vector of TrackInfo.
inline tracksInfoList_t enumerateTrackInfoList(ITV8::GDRV::Storage::ITrackInfoEnumerator* trackEnumerator);

/// Implementation of ITV8::GDRV::Storage::ITrackInfoEnumerator.
typedef BasicEnumeratorImpl<Storage::ITrackInfoEnumerator, tracksInfoList_t> TrackInfoEnumerator;

/// Implementation of ITV8::GDRV::Storage::IRecordingInfo interface
struct RecordingInfo : public GDRV::Storage::IRecordingInfo
{
    RecordingInfo() : 
        m_enumerator(tracks),
        m_presentationRange()
    {
    }

    explicit RecordingInfo(GDRV::Storage::IRecordingInfo* info) :
        id(info->GetId()),
        description(safeCopyString(info->GetDescription())),
        tracks(enumerateTrackInfoList(info->GetTracksEnumerator())),
        m_enumerator(tracks),
        m_presentationRange(info->GetMediaPresentationRange())
    {
    }

    virtual const char* GetId() const { return id.c_str(); }
    virtual const char* GetDescription() const { return description.c_str(); }
    virtual Storage::ITrackInfoEnumerator* GetTracksEnumerator() { return &m_enumerator; }
    virtual const DateTimeRange& GetMediaPresentationRange() const {return m_presentationRange; }

    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY(Storage::IRecordingInfo)
        ITV8_CONTRACT_ENTRY(IContract)
    ITV8_END_CONTRACT_MAP()

    std::string                    id;
    std::string                    description;
    tracksInfoList_t            tracks;
    TrackInfoEnumerator            m_enumerator;
    DateTimeRange                 m_presentationRange;
};

typedef boost::shared_ptr<RecordingInfo> RecordingInfoSP;
typedef std::vector<RecordingInfoSP> recordingInfoList;

typedef BasicEnumeratorImpl<Storage::IRecordingInfoEnumerator, 
    recordingInfoList> RecordingInfoEnumerator;

/// Implementation of ITV8::GDRV::Storage::ITrackRange interface.
class TrackRange : public Storage::ITrackRange
{
public:
    TrackRange(const std::string& trackId, const DateTimeRange& trackRange) : 
        id(trackId),
        range(trackRange)
    {}

    virtual const char* GetId() const { return id.c_str(); }
    virtual const DateTimeRange& GetRange() const { return range; }

    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY(Storage::ITrackRange)
        ITV8_CONTRACT_ENTRY(IContract)
    ITV8_END_CONTRACT_MAP()

    std::string            id;
    DateTimeRange        range;
};

typedef std::vector<TrackRange> tracksRangeList_t;

/// Implementation of ITV8::GDRV::Storage::ITracksRangeEnumerator
typedef BasicEnumeratorImpl<Storage::ITracksRangeEnumerator, tracksRangeList_t> TracksRangeEnumerator;

/// Implementation of ITV8::GDRV::Storage::IRecordingRange interface.
class RecordingRange : public Storage::IRecordingRange
{
public:
    RecordingRange(const std::string& recId, int recStatus) :
        id(recId),
        status(recStatus),
        tracksRangeEnum(tracksRange)
    {}
    
    virtual const char* GetId() const { return id.c_str(); }
    virtual ITV8::uint32_t GetStatus() const { return status; }
    virtual Storage::ITracksRangeEnumerator* GetTracksRangeEnumerator() { return &tracksRangeEnum; }

    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY(Storage::IRecordingRange)
        ITV8_CONTRACT_ENTRY(IContract)
    ITV8_END_CONTRACT_MAP()
    
    std::string                    id;
    int                            status;
    tracksRangeList_t            tracksRange;
    TracksRangeEnumerator        tracksRangeEnum;
};

inline tracksInfoList_t enumerateTrackInfoList(ITV8::GDRV::Storage::ITrackInfoEnumerator* trackEnumerator)
{
    if (!trackEnumerator)
    {
        return tracksInfoList_t();
    }

    tracksInfoList_t result;
    trackEnumerator->Reset();
    while (trackEnumerator->MoveNext())
    {
        if (ITV8::GDRV::Storage::ITrackInfo* current = trackEnumerator->GetCurrent())
        {
            result.push_back(TrackInfo(current));
        }
    }
    return result;
}

typedef std::vector<std::string> tracksList_t;
/// Pushes track id-s from IPINT interface enumerator to vector of string.
inline tracksList_t enumerateTracks(ITV8::GDRV::Storage::ITrackIdEnumerator* trackEnumerator)
{
    if (!trackEnumerator)
    {
        return tracksList_t();
    }
    tracksList_t result;
    trackEnumerator->Reset();
    while (trackEnumerator->MoveNext())
    {
        result.push_back(safeCopyString(trackEnumerator->GetCurrent()));
    }
    return result;
}

typedef Utility::BasicEnumeratorImpl<GDRV::Storage::ITrackIdEnumerator,
    tracksList_t> TrackIdEnum;

typedef std::vector<ITV8::timestamp_t> calendarList_t;

}
}

#endif
