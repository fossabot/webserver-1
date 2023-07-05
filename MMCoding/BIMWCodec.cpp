#include "TweakableFilterImpl.h"
#include "FrameBuilder.h"
#include "Transforms.h"

#ifdef _WIN32

#include <boost/type_erasure/any_cast.hpp>

#include "BIMWCodec.h"

namespace {

class CDownscaleTransform : public NLogging::WithLogger
{
public:
    CDownscaleTransform( DECLARE_LOGGER_ARG, NMMSS::NAugment::BIMWDownscale const& aug )
    : WithLogger( GET_LOGGER_PTR )
    , m_transform( aug.width, aug.height )
    {
        _dbg_ << "Creating motion wavelet transform. this=" << this
            << " width=" << aug.width
            << " height=" << aug.height;
    }
    ~CDownscaleTransform()
    {
        _dbg_ << "Destroying motion wavelet transform. this=" << this;
    }
    CDownscaleTransform( CDownscaleTransform && other )
    : WithLogger( std::move(other) )
    , m_transform( std::move(other.m_transform) )
    {}
    void Tweak( NMMSS::CAugment const& aug )
    {
        auto const& a = boost::type_erasure::any_cast<NMMSS::NAugment::BIMWDownscale>(aug);
        boost::unique_lock<boost::mutex> lock(m_mutex);
        m_transform.Tweak( a.width, a.height );
        _dbg_ << "Tweaked motion wavelet transform. this=" << this
            << " width=" << a.width
            << " height=" << a.height;
    }
    NMMSS::CAugment GetTweak()
    {
        UInt16 w, h;
        m_transform.GetTweak(&w, &h);
        return NMMSS::NAugment::BIMWDownscale{w, h};
    }
    NMMSS::ETransformResult operator()( NMMSS::ISample * sample, NMMSS::CDeferredAllocSampleHolder & holder )
    {
        boost::unique_lock<boost::mutex> lock(m_mutex);
        return m_transform(sample,holder);
    }
private:
    NMMSS::CBIWMDownscale m_transform;
    boost::mutex mutable m_mutex;
};

} // anonymous namespace

#endif // _WIN32

namespace NMMSS {

ITweakableFilter *
CreateBIMWDownscaleFilter(
    DECLARE_LOGGER_ARG,
    NAugment::BIMWDownscale const& aug )
{
#ifdef _WIN32
    return new CTweakableFilterImpl<CDownscaleTransform>(
        GET_LOGGER_PTR, new CDownscaleTransform(GET_LOGGER_PTR, aug));
#else
    throw std::runtime_error(std::string(__FUNCTION__) + " is not implemented yet on the platform");
#endif
}

} // namespace NMMSS
