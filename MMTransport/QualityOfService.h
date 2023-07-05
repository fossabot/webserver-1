#ifndef MMTRANSPORT_QUALITYOFSERVICE_H_
#define MMTRANSPORT_QUALITYOFSERVICE_H_

#include <cassert>
#include <MMIDL/MMQualityOfServiceC.h>

namespace detail {

template< class TQoSRequest >
struct QoSRequestTrait
{
    static MMSS::EQoSRequest id();
    static void set(MMSS::UQoSRequest&, TQoSRequest const&);
    static TQoSRequest & get(MMSS::UQoSRequest&);
    static TQoSRequest const& get(MMSS::UQoSRequest const&);
};

#define QOS_REQUEST_TRAITS_IMPL(request_type, ID, accessor) \
template<> struct QoSRequestTrait< request_type > { \
    static MMSS::EQoSRequest id() { return ID; } \
    static void set(MMSS::UQoSRequest& u, request_type const& r) { u.accessor(r); } \
    static request_type & get(MMSS::UQoSRequest& u) { return u.accessor(); } \
    static request_type const & get(MMSS::UQoSRequest const& u) { return u.accessor(); } \
}

QOS_REQUEST_TRAITS_IMPL( MMSS::QoSRequest::OnlyKeyFrames, MMSS::QOS_OnlyKeyFrames, onlyKeyFrames );
QOS_REQUEST_TRAITS_IMPL( MMSS::QoSRequest::FrameRate, MMSS::QOS_FrameRate, frameRate );
QOS_REQUEST_TRAITS_IMPL( MMSS::QoSRequest::FrameGeometry, MMSS::QOS_FrameGeometry, frameGeometry );
QOS_REQUEST_TRAITS_IMPL( MMSS::QoSRequest::Buffer, MMSS::QOS_Buffer, buffer );
QOS_REQUEST_TRAITS_IMPL( MMSS::QoSRequest::StartFrom, MMSS::QOS_StartFrom, startFrom );
QOS_REQUEST_TRAITS_IMPL( MMSS::QoSRequest::DecoderRequirements, MMSS::QOS_DecoderRequirements, decoderRequirements);
QOS_REQUEST_TRAITS_IMPL( MMSS::QoSRequest::PlaybackDepth, MMSS::QOS_PlaybackDepth, playbackDepth);

#undef QOS_REQUEST_TRAITS_IMPL

template< class TQoSRequest >
MMSS::UQoSRequest ToUnion( TQoSRequest const& r )
{
    using trait = QoSRequestTrait<TQoSRequest>;
    MMSS::UQoSRequest u;
    trait::set(u, r);
    return u;
}

template< class TQoSRequest >
TQoSRequest & FromUnion( MMSS::UQoSRequest & u )
{
    using trait = QoSRequestTrait<TQoSRequest>;
    assert( u._d() == trait::id() );
    return trait::get(u);
}

template< class TQoSRequest >
TQoSRequest const& FromUnion( MMSS::UQoSRequest const& u )
{
    using trait = QoSRequestTrait<TQoSRequest>;
    assert( u._d() == trait::id() );
    return trait::get(u);
}

template< class TVisitor, class... TArgs >
typename TVisitor::result_type ApplyVisitor( TVisitor& visitor, MMSS::UQoSRequest& u, TArgs&&... args )
{
    switch( u._d() )
    {
    case MMSS::QOS_OnlyKeyFrames:
        return visitor( FromUnion<MMSS::QoSRequest::OnlyKeyFrames>(u), std::forward<TArgs>(args)... );
    case MMSS::QOS_FrameRate:
        return visitor( FromUnion<MMSS::QoSRequest::FrameRate>(u), std::forward<TArgs>(args)... );
    case MMSS::QOS_FrameGeometry:
        return visitor( FromUnion<MMSS::QoSRequest::FrameGeometry>(u), std::forward<TArgs>(args)... );
    case MMSS::QOS_Buffer:
        return visitor( FromUnion<MMSS::QoSRequest::Buffer>(u), std::forward<TArgs>(args)... );
    case MMSS::QOS_StartFrom:
        return visitor( FromUnion<MMSS::QoSRequest::StartFrom>(u), std::forward<TArgs>(args)... );
    case MMSS::QOS_DecoderRequirements:
        return visitor(FromUnion<MMSS::QoSRequest::DecoderRequirements>(u), std::forward<TArgs>(args)...);
    case MMSS::QOS_PlaybackDepth:
        return visitor(FromUnion<MMSS::QoSRequest::PlaybackDepth>(u), std::forward<TArgs>(args)...);
    default:
        throw std::runtime_error("QualityOfService: Unknown QoSRequest type");
    }
}

template< class TVisitor, class... TArgs >
typename TVisitor::result_type ApplyVisitor( TVisitor& visitor, MMSS::UQoSRequest const& u, TArgs&&... args )
{
    switch( u._d() )
    {
    case MMSS::QOS_OnlyKeyFrames:
        return visitor( FromUnion<MMSS::QoSRequest::OnlyKeyFrames>(u), std::forward<TArgs>(args)... );
    case MMSS::QOS_FrameRate:
        return visitor( FromUnion<MMSS::QoSRequest::FrameRate>(u), std::forward<TArgs>(args)... );
    case MMSS::QOS_FrameGeometry:
        return visitor( FromUnion<MMSS::QoSRequest::FrameGeometry>(u), std::forward<TArgs>(args)... );
    case MMSS::QOS_Buffer:
        return visitor( FromUnion<MMSS::QoSRequest::Buffer>(u), std::forward<TArgs>(args)... );
    case MMSS::QOS_StartFrom:
        return visitor( FromUnion<MMSS::QoSRequest::StartFrom>(u), std::forward<TArgs>(args)... );
    case MMSS::QOS_DecoderRequirements:
        return visitor(FromUnion<MMSS::QoSRequest::DecoderRequirements>(u), std::forward<TArgs>(args)...);
    case MMSS::QOS_PlaybackDepth:
        return visitor(FromUnion<MMSS::QoSRequest::PlaybackDepth>(u), std::forward<TArgs>(args)...);
    default:
        throw std::runtime_error("QualityOfService: Unknown QoSRequest type");
    }
}

template< unsigned int t_idx >
void FillRequests( MMSS::QualityOfService & )
{}
template< unsigned int t_idx = 0, class TQoSRequest, class... TForwards >
void FillRequests( MMSS::QualityOfService & qos, TQoSRequest const& r, TForwards &&... fwds )
{
    qos[t_idx] = ToUnion(r);
    FillRequests<t_idx+1>( qos, std::forward<TForwards>(fwds)... );
}

inline void push_back( MMSS::QualityOfService & qos, MMSS::UQoSRequest const& u )
{
    auto const len = qos.length();
    qos.length( len+1 );
    qos[len] = u;
}

class Compare
{
    MMSS::UQoSRequest const& m_lhs;
public:
    using result_type = bool;

    Compare( MMSS::UQoSRequest const& lhs )
        : m_lhs( lhs )
    {}
    template< class TQoSRequest >
    bool operator()( TQoSRequest const& rhs )
    {
        if( m_lhs._d() != detail::QoSRequestTrait<TQoSRequest>::id() )
            return false;
        TQoSRequest const& l = detail::FromUnion<TQoSRequest>( m_lhs );
        return l == rhs;
    }
};

} // namespace detail

namespace NMMSS {

template< class... TQoSRequests >
MMSS::QualityOfService MakeQualityOfService( TQoSRequests&&... requests )
{
    auto qos = MMSS::QualityOfService();
    qos.length( sizeof...(TQoSRequests) );
    detail::FillRequests<0>( qos, std::forward<TQoSRequests>(requests)... );
    return qos;
}

template< class TQoSRequest >
void RemoveRequest( MMSS::QualityOfService & qos )
{
    using trait = detail::QoSRequestTrait<TQoSRequest>;
    CORBA::ULong i = 0;
    CORBA::ULong len = qos.length();
    for( ; i<len; ++i )
        if( qos[i]._d() == trait::id() )
            break;
    for( ; i+1<len; ++i )
        qos[i] = qos[i+1];
    qos.length(i);
}

template< class TQoSRequest >
TQoSRequest * GetRequest( MMSS::QualityOfService & qos )
{
    using trait = detail::QoSRequestTrait<TQoSRequest>;
    for( CORBA::ULong i=0, len=qos.length(); i<len; ++i )
    {
        auto & u = qos[i];
        if( u._d() == trait::id() )
            return &detail::FromUnion<TQoSRequest>(u);
    }
    return nullptr;
}
template< class TQoSRequest >
TQoSRequest const* GetRequest( MMSS::QualityOfService const& qos )
{
    using trait = detail::QoSRequestTrait<TQoSRequest>;
    for( CORBA::ULong i=0, len=qos.length(); i<len; ++i )
    {
        auto const& u = qos[i];
        if( u._d() == trait::id() )
            return &detail::FromUnion<TQoSRequest>(u);
    }
    return nullptr;
}
template< class TQoSRequest >
void SetRequest( MMSS::QualityOfService & qos, TQoSRequest const& r )
{
    if( auto req = GetRequest<TQoSRequest>(qos) )
    {
        *req = r;
        return;
    }
    detail::push_back( qos, detail::ToUnion(r) );
}
template< class TVisitor >
void ApplyVisitor( MMSS::QualityOfService & qos, TVisitor & visitor )
{
    for( CORBA::ULong i=0, len=qos.length(); i<len; ++i )
        detail::ApplyVisitor( visitor, qos[i] );
}
template< class TVisitor >
void ApplyVisitor( MMSS::QualityOfService const& qos, TVisitor & visitor )
{
    for( CORBA::ULong i=0, len=qos.length(); i<len; ++i )
        detail::ApplyVisitor( visitor, qos[i] );
}

} // namespace NMMSS

namespace MMSS {
namespace QoSRequest {

inline bool operator==( OnlyKeyFrames const& lhs, OnlyKeyFrames const& rhs )
{ return lhs.enabled==rhs.enabled; }
inline bool operator!=( OnlyKeyFrames const& lhs, OnlyKeyFrames const& rhs )
{ return !(lhs==rhs); }

inline bool operator==( FrameRate const& lhs, FrameRate const& rhs )
{ return lhs.fps==rhs.fps; }
inline bool operator!=( FrameRate const& lhs, FrameRate const& rhs )
{ return !(lhs==rhs); }

inline bool operator==( FrameGeometry const& lhs, FrameGeometry const& rhs )
{ return lhs.width==rhs.width && lhs.height==rhs.height; }
inline bool operator!=( FrameGeometry const& lhs, FrameGeometry const& rhs )
{ return !(lhs==rhs); }

inline bool operator==( Buffer const& lhs, Buffer const& rhs )
{ return lhs.duration==rhs.duration && lhs.start==rhs.start; }
inline bool operator!=( Buffer const& lhs, Buffer const& rhs )
{ return !(lhs==rhs); }

inline bool operator==( StartFrom const& lhs, StartFrom const& rhs )
{ return lhs.from==rhs.from; }
inline bool operator!=( StartFrom const& lhs, StartFrom const& rhs )
{ return !(lhs==rhs); }

inline bool operator==(const DecoderRequirements& lhs, const DecoderRequirements& rhs)
{ 
    return lhs.deviceTypeMask == rhs.deviceTypeMask &&
        lhs.deviceIdMask == rhs.deviceIdMask &&
        lhs.memoryTypeMask == rhs.memoryTypeMask &&
        lhs.targetProcessId == rhs.targetProcessId;
}

inline bool operator==(PlaybackDepth const& lhs, PlaybackDepth const& rhs)
{
    return lhs.seconds == rhs.seconds;
}
inline bool operator!=(PlaybackDepth const& lhs, PlaybackDepth const& rhs)
{
    return !(lhs == rhs);
}

} // namespace QoSRequest

inline bool operator==( QualityOfService const& lhs, QualityOfService const& rhs )
{
    auto len = lhs.length();
    if( len != rhs.length() )
        return false;
    for( CORBA::ULong i=0; i<len; ++i )
    {
        detail::Compare cmp(lhs[i]);
        if( !detail::ApplyVisitor( cmp, rhs[i] ) )
            return false;
    }
    return true;
}

inline bool operator!=( QualityOfService const& lhs, QualityOfService const& rhs )
{
    return !(lhs == rhs);
}

} // namespace MMSS

#endif // MMTRANSPORT_QUALITYOFSERVICE_H_
